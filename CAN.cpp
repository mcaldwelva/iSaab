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
 *  03/04/2016 Mike C. - Modified send function:
 *   guarantees transmission order of up to 12 consecutive messages
 */

#include <SPI.h>
#include <utility/Sd2PinMap.h>
#include "CAN.h"

#define MCP2515_SPI_SETTING SPISettings(10000000, MSBFIRST, SPI_MODE0)

// setup pins, connect to CAN bus at the requested speed, begin accepting messages
void CAN::begin(uint16_t speed) {
  pinMode(MCP2515_CS, OUTPUT);
  digitalWrite(MCP2515_CS, HIGH);

  pinMode(MCP2515_IRQ, INPUT);

  SPI.begin();

  // reset MCP2515 to clear registers and put it into configuration mode
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  spiwrite(SPI_RESET);

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  // wait for reset to complete
  delayMicroseconds(10);

  // configure bus speed
  uint8_t cnf1, cnf2, cnf3;
  switch (speed)
  {
    case 47:
      // http://www.microchip.com/forums/m/tm.aspx?m=236967&p=2
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

  // only RXB0 activates interrupt
  mcp2515_write_register(CANINTE, _BV(RX0IE));

  // RXB0: accept all messages, allow rollover to RXB1
  mcp2515_write_register(RXB0CTRL, _BV(RXM1) | _BV(RXM0) | _BV(BUKT));
  // RXB1: accept all messages
  mcp2515_write_register(RXB1CTRL, _BV(RXM1) | _BV(RXM0));

  // light up the corresponding LED when a buffer is occupied
  mcp2515_write_register(BFPCTRL, _BV(B1BFE) | _BV(B1BFM) | _BV(B0BFE) | _BV(B0BFM));

  // standard operating mode
  setMode(NORMAL_MODE);
}


// transmit a message in the order it was presented
bool CAN::send(const msg &message) {
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
    return false;
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

  return true;
}


// read the highest priority message available and clear its buffer
bool CAN::receive(msg &message) {
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
    return false;
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
  message.header.rtr = bit_is_set(status, RXRTR) ? 1 : 0;

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

  return true;
}


// change the operating mode of the can chip
// NORMAL_MODE, SLEEP_MODE, LOOPBACK_MODE, LISTEN_ONLY_MODE, CONFIG_MODE
void CAN::setMode(uint8_t mode) {
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


// enable message id filtering
void CAN::setFilters(const uint16_t filters[] PROGMEM, const uint16_t masks[] PROGMEM) {
  uint16_t filter;
  setMode(CONFIG_MODE);

  // set high priority filter id's for RXB0
  filter = pgm_read_word_near(filters + 0);
  mcp2515_write_register(RXF0SIDH, filter >> 3);
  mcp2515_write_register(RXF0SIDL, filter << 5);

  filter = pgm_read_word_near(filters + 1);
  mcp2515_write_register(RXF1SIDH, filter >> 3);
  mcp2515_write_register(RXF1SIDL, filter << 5);

  // set high pirority bit mask for RXB0
  filter = pgm_read_word_near(masks + 0);
  mcp2515_write_register(RXM0SIDH, filter >> 3);
  mcp2515_write_register(RXM0SIDL, filter << 5);

  // set low priority filter id's for RXB1
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

  // set low priority bit mask for RXB1
  filter = pgm_read_word_near(masks + 1);
  mcp2515_write_register(RXM1SIDH, filter >> 3);
  mcp2515_write_register(RXM1SIDL, filter << 5);

  // RXB0: accept only standard id's that match the high priority filters
  mcp2515_bit_modify(RXB0CTRL, _BV(RXM1) | _BV(RXM0), _BV(RXM0));
  // RXB1: accept only standard id's that match the low priority filters
  mcp2515_bit_modify(RXB1CTRL, _BV(RXM1) | _BV(RXM0), _BV(RXM0));

  setMode(NORMAL_MODE);
}


// enable/disable interrupts for low priority messages
void CAN::setLowPriorityInterrupts(bool enabled) {
  mcp2515_bit_modify(CANINTE, _BV(RX1IE), enabled ? 0xff : 0x00);
}


void CAN::mcp2515_write_register( uint8_t address, uint8_t data ) {
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


uint8_t CAN::mcp2515_read_status(uint8_t type) {
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


void CAN::mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data) {
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


uint8_t CAN::mcp2515_read_register(uint8_t address) {
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


inline __attribute__((always_inline))
uint8_t CAN::spiread() {
  return SPI.transfer(0x00);
}

inline __attribute__((always_inline))
void CAN::spiwrite(uint8_t c) {
  SPI.transfer(c);
}


