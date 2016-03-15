/*
 *  CONTROLLER AREA NETWORK (CAN 2.0A STANDARD ID)
 *  CAN BUS library for Wiring/Arduino 1.1
 *  ADAPTED FROM http://www.kreatives-chaos.com
 *  By IGOR REAL (16 - 05 - 2011)
 *
 *  08/24/2015 Mike C. - Number of changes:
 *   1. Use SPI transactions to allow bus sharing
 *   2. Use Arduino pin macros for portability and readability
 *   3. Code simplification, C++ style pass by reference
 *   4. Removed global vars and buffer implementation to save memory
 *   5. Added Wake-on-CAN and toggle both LED's
 *
 *  10/09/2015 Mike C. - Adjusted speed settings:
 *  http://www.microchip.com/forums/m/tm.aspx?m=236967&p=2
 *
 *  03/04/2016 Mike C. - Modified send function:
 *   guarantees transmission order of up to 12 consecutive messages
 */

#include <SPI.h>
#include <utility/Sd2PinMap.h>
#include "CAN.h"

#define MCP2515_SPI_SETTING SPISettings(10000000, MSBFIRST, SPI_MODE0)

/******************************************************************************
 * PUBLIC METHODS
 ******************************************************************************/
void CAN::begin(uint16_t speed) {
  pinMode(MCP2515_CS, OUTPUT);
  digitalWrite(MCP2515_CS, HIGH);

  pinMode(MCP2515_IRQ, INPUT);

  SPI.begin();

  // reset MCP2515 by software reset.
  // After this he is in configuration mode.
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(SPI_RESET);

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  // wait a little bit until the MCP2515 has restarted
  delayMicroseconds(10);

  // configure bus speed
  uint8_t cnf1, cnf2, cnf3;
  switch (speed)
  {
    case 47:
      cnf1 = 0xc7; cnf2 = 0xbe; cnf3 = 0x44;
      break;
    case 1:
      cnf1 = 0x00; cnf2 = 0x90; cnf3 = 0x02;
      break;
    case 500:
      cnf1 = 0x01; cnf2 = 0x90; cnf3 = 0x02;
      break;
    case 250:
      cnf1 = 0x01; cnf2 = 0xB8; cnf3 = 0x05;
      break;
    case 125:
      cnf1 = 0x07; cnf2 = 0x90; cnf3 = 0x02;
      break;
    case 100:
      cnf1 = 0x03; cnf2 = 0xBA; cnf3 = 0x07;
      break;
    default:
      cnf1 = 0x00; cnf2 = 0x90; cnf3 = 0x02;
      break;
  }
  mcp2515_write_register(CNF1, cnf1);
  mcp2515_write_register(CNF2, cnf2);
  mcp2515_write_register(CNF3, cnf3);

  //Activamos Interrupcion de RX
  mcp2515_write_register(CANINTE, _BV(RX0IE)); // Only buffer 0 activates interrupt

  //Filtros
  //Bufer 0: Todos los msjes y Rollover=>Si buffer 0 lleno,envia a buffer 1
  mcp2515_write_register(RXB0CTRL, _BV(RXM1) | _BV(RXM0) | _BV(BUKT)); //RXM1 y RXM0 para filter/mask off+Rollover
  //Bufer 1: Todos los msjes
  mcp2515_write_register(RXB1CTRL, _BV(RXM1) | _BV(RXM0)); //RXM1 y RXM0 para filter/mask off

  // light up the corresponding LED when a buffer is occupied
  mcp2515_write_register(BFPCTRL, _BV(B1BFE) | _BV(B1BFM) | _BV(B0BFE) | _BV(B0BFM));

  //Pasar el MCP2515 a modo normal
  setMode(NORMAL_MODE);
}
// ----------------------------------------------------------------------------
/*
Name:send(message)
Parameters(type):
	message(mesCAN):message to be sent
Description:
	It sends through the bus the message passed by reference
Returns:
	0xFF: if 2515's tx-buffer is full
	0x01: if when 2515's TxBuffer[0] used
	0x02: if when 2515's TxBuffer[1] used
	0x04: if when 2515's TxBuffer[2] used
Example:
	uint8_t count = 0;
	while(CAN.send(CAN_TxMsg)==0xFF && count < 15);

*/
uint8_t CAN::send(const msg &message) {
  static uint8_t id;
  uint8_t status = mcp2515_read_status(SPI_READ_STATUS);

  // reset id if all buffers are clear
  if ((status & 0b01010100) == 0) {
    id = 14;
  }

  // calculate TX buffer address & priority
  uint8_t address = (id & 0x03) * 2;
  uint8_t priority = id >> 2;

  // if that buffer isn't available we're done
  if (bit_is_set(status, address + 2)) {
    return 0xff;
  }

  // decrement id, skipping invalid addresses
  id -= (id % 4) ? 1 : 2;

#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  // select buffer
  spiwrite(SPI_WRITE_TX | address);

  // msg id
  spiwrite(message.id >> 3);
  spiwrite(message.id << 5);

  // no extended id
  spiwrite(0x00);
  spiwrite(0x00);

  uint8_t length = message.header.length & 0x0f;
  if (message.header.rtr) {
    // a rtr-frame has a length, but contains no data
    spiwrite(_BV(RTR) | length);
  }
  else {
    // set message length
    spiwrite(length);

    // data
    for (uint8_t t = 0; t < length; t++) {
      spiwrite(message.data[t]);
    }
  }

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  // select the associated control register
  uint8_t ctrlreg;
  switch (address) {
    case 4:
      ctrlreg = TXB2CTRL;
      break;
    case 2:
      ctrlreg = TXB1CTRL;
      break;
    case 0:
      ctrlreg = TXB0CTRL;
      break;
  }

  // flag the buffer for transmission
  mcp2515_bit_modify(ctrlreg, _BV(TXREQ) | _BV(TXP1) | _BV(TXP0), _BV(TXREQ) | priority);

  return address;
}
// ----------------------------------------------------------------------------
/*
Name:receive(message)
Parameters(type):
	message(msg): Message passed by reference to be filled with the new
			  message coming fron the 2515 drvier
Description:
	Receives by parameter a struct msgCAN
Returns(uint8_t):
	last 3 bits of 2515's status. More in the
Example:
	if(CAN.receive(message))
	{
	}
*/
uint8_t CAN::receive(msg &message) {
  uint8_t address;

  // buffer 0 has the higher priority
  uint8_t status = mcp2515_read_status(SPI_RX_STATUS);
  if (bit_is_set(status, 6)) {
    // message in buffer 0
    address = 0x00;
  } else if (bit_is_set(status, 7)) {
    // message in buffer 1
    address = 0x04;
  } else {
    // no message available
    return 0x00;
  }

#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(SPI_READ_RX | address);

  // read id
  message.id  = spiread() << 3;
  message.id |= spiread() >> 5;

  // skip extended id
  spiread();
  spiread();

  // read DLC
  uint8_t length = spiread() & 0x0f;
  message.header.length = length;
  message.header.rtr = bit_is_set(status, 3) ? 1 : 0;

  // read data
  for (uint8_t t = 0; t < length; t++) {
    message.data[t] = spiread();
  }

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  // clear interrupt flag
  uint8_t flag;
  if (address) {
    flag = _BV(RX1IF);
  }	else {
    flag = _BV(RX0IF);
  }
  mcp2515_bit_modify(CANINTF, flag, 0x00);

  return flag;
}
// ----------------------------------------------------------------------------
/*
Name: available()
Parameters(type):
	None
Description:
	Polls interrption bit
Returns:
	0: if there is not messages waiting in the converter
	1: if there is
Example:
	if (CAN.available())

*/
uint8_t CAN::available()
{
  return (!fastDigitalRead(MCP2515_IRQ));
}
// ----------------------------------------------------------------------------
/*
Name: setMode(mode)

Parameters(type):
	uint8_t mode

Description:
	The MCP2515 has five modes of operation. This function configures:
		0. Normal mode
		1. Sleep mode
		2. Loopback mode
		3. Listen-Only mode
		4. Configuration mode

Returns:
	Nothing

Example:
	CAN.setMode(SLEEP_MODE);
*/
void CAN::setMode(uint8_t mode)
{
  // don't set invalid modes
  if (mode > CONFIG_MODE)
    return;

  // enable/disable Wake-on-CAN
  switch (mode) {
    case SLEEP_MODE:
      mcp2515_bit_modify(CANINTE, _BV(WAKIE), _BV(WAKIE));
      break;
    case NORMAL_MODE:
      mcp2515_bit_modify(CANINTE, _BV(WAKIE), 0x00);
      mcp2515_bit_modify(CANINTF, _BV(WAKIF), 0x00);
      break;
  }

  // set the new mode
  mcp2515_bit_modify(CANCTRL, _BV(REQOP2) | _BV(REQOP1) | _BV(REQOP0), mode);

  // wait until the mode has been changed
  while ((mcp2515_read_register(CANSTAT) & (_BV(REQOP2) | _BV(REQOP1) | _BV(REQOP0))) != mode);
}
// ----------------------------------------------------------------------------
/*
Name: setFilter

Parameters(type):
	uint8_t mode

Description:


Returns:
	Nothing

Example:

*/
void CAN::setFilters(const uint16_t filters[] PROGMEM, const uint16_t masks[] PROGMEM)
{
  uint16_t filter;
  setMode(CONFIG_MODE);

  //Buffer RXB0 => Filter 0-1 & Mask 0
  filter = pgm_read_word_near(filters + 0);
  mcp2515_write_register(RXF0SIDH, filter >> 3);
  mcp2515_write_register(RXF0SIDL, filter << 5);

  filter = pgm_read_word_near(filters + 1);
  mcp2515_write_register(RXF1SIDH, filter >> 3);
  mcp2515_write_register(RXF1SIDL, filter << 5);

  //Mask 0
  filter = pgm_read_word_near(masks + 0);
  mcp2515_write_register(RXM0SIDH, filter >> 3);
  mcp2515_write_register(RXM0SIDL, filter << 5);
  //---------------------------------------------------------------

  //Buffer RXB1 => Filter 2-5 & Mask 1
  filter = pgm_read_word_near(filters + 2);
  mcp2515_write_register(RXF2SIDH, filter >> 3);
  mcp2515_write_register(RXF2SIDL, filter << 5);

  filter = pgm_read_word_near(filters + 3);
  mcp2515_write_register(RXF3SIDH, filter >> 3);
  mcp2515_write_register(RXF3SIDL, filter << 5);

  filter = pgm_read_word_near(filters + 4);
  mcp2515_write_register(RXF4SIDH, filter >> 3);
  mcp2515_write_register(RXF4SIDL, filter << 5);

  filter = pgm_read_word_near(filters + 5);
  mcp2515_write_register(RXF5SIDH, filter >> 3);
  mcp2515_write_register(RXF5SIDL, filter << 5);

  // Mask1
  filter = pgm_read_word_near(masks + 1);
  mcp2515_write_register(RXM1SIDH, filter >> 3);
  mcp2515_write_register(RXM1SIDL, filter << 5);


  //Buffer configuration
  //Bufer 0
  mcp2515_bit_modify(RXB0CTRL, _BV(RXM1) | _BV(RXM0), _BV(RXM0));
  //Bufer 1
  mcp2515_bit_modify(RXB1CTRL, _BV(RXM1) | _BV(RXM0), _BV(RXM0));

  setMode(NORMAL_MODE);
}
// ----------------------------------------------------------------------------

// enable/disable interrupts for low priority messages
void CAN::setLowPriorityInterrupts(bool enabled)
{
  mcp2515_bit_modify(CANINTE, _BV(RX1IE), enabled ? 0xff : 0x00);
}







/******************************************************************************
 * PRIVATE METHODS
 ******************************************************************************/

// -------------------------------------------------------------------------
inline __attribute__((always_inline))
uint8_t CAN::spiread()
{
  return SPI.transfer(0x00);
}

inline __attribute__((always_inline))
void CAN::spiwrite(uint8_t c)
{
  SPI.transfer(c);
}

// -------------------------------------------------------------------------
void CAN::mcp2515_write_register( uint8_t address, uint8_t data )
{
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(SPI_WRITE);
  spiwrite(address);
  spiwrite(data);

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif
}
// ----------------------------------------------------------------------------
uint8_t CAN::mcp2515_read_status(uint8_t type)
{
  uint8_t data;

#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(type);
  data = spiread();

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  return data;
}
// -------------------------------------------------------------------------
void CAN::mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data)
{
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(SPI_BIT_MODIFY);
  spiwrite(address);
  spiwrite(mask);
  spiwrite(data);

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif
}
// ----------------------------------------------------------------------------
uint8_t CAN::mcp2515_check_free_buffer()
{
  uint8_t status = mcp2515_read_status(SPI_READ_STATUS);

  if ((status & 0x54) == 0x54) {
    // all buffers used
    return false;
  }

  return true;
}
// ----------------------------------------------------------------------------
uint8_t CAN::mcp2515_read_register(uint8_t address)
{
  uint8_t data;

#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(SPI_READ);
  spiwrite(address);
  data = spiread();

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  return data;
}
// ----------------------------------------------------------------------------
