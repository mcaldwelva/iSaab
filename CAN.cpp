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
 *  03/04/2016 Mike C. - More changes:
 *   1. send: guarantees transmission order of up to 12 consecutive messages
 *   2. receive: get RTR bit from the right place
 *   3. simplified public interface
 *
 *  10/12/2016 Mike C. - Fixed clearing Rx buffers
 *  10/06/2017 Mike C. - Use RX1BF to manage transceiver
 */

#include <SPI.h>
#include <utility/Sd2PinMap.h>
#include "CAN.h"

#define MCP2515_SPI_SETTING SPISettings(10000000, MSBFIRST, SPI_MODE0)

// setup pins, set CAN bus speed, optionally set filters, begin accepting messages
void CAN::begin(uint16_t speed, const uint16_t high[] PROGMEM, const uint16_t low[] PROGMEM) {
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
  switch (speed) {
    case 47:
      // http://www.microchip.com/forums/m/tm.aspx?m=236967&p=2
      cnf1 = 0xc7; cnf2 = 0xbe; cnf3 = 0x44;
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
  writeRegister(CNF1, cnf1);
  writeRegister(CNF2, cnf2);
  writeRegister(CNF3, cnf3);

  // always interrupt on high priority RX
  writeRegister(CANINTE, _BV(RX0IE));

  // allow rollover from RXB0 to RXB1
  modifyRegister(RXB0CTRL, _BV(BUKT), 0xff);

  // set RX1BF to put transceiver in standby mode
  writeRegister(BFPCTRL, _BV(B1BFS) | _BV(B1BFE));

  // configure filters
  setFilters(high, low);

  // standard operating mode
  setMode(Normal);
}


// transmit a message in the order it was presented
bool CAN::send(const msg &message) {
  static uint8_t id;
  uint8_t status = readStatus(SPI_READ_STATUS);

  // reset id if all buffers are clear
  if ((status & 0b01010100) == 0) {
    id = 14;
  }

  // calculate TX buffer address
  uint8_t address = (id & 0b11) << 1;

  // if that buffer isn't available we're done
  if (bit_is_set(status, address + 2)) {
    return false;
  }

#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(MCP2515_SPI_SETTING);
#endif
  fastDigitalWrite(MCP2515_CS, LOW);

  // select buffer
  spiwrite(SPI_WRITE_TX | address);

  // standard id
  spiwrite(message.id >> 3);
  spiwrite(message.id << 5);

  // no extended id
  spiwrite(0x00);
  spiwrite(0x00);

  // DLC
  uint8_t length = message.header.length;
  if (message.header.rtr) {
    spiwrite(_BV(RTR) | length);
  } else {
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

  // calculate the control register and priority
  uint8_t ctrlreg = (address << 3) + TXB0CTRL;
  uint8_t priority = id >> 2;

  // flag the buffer for transmission
  modifyRegister(ctrlreg, _BV(TXREQ) | _BV(TXP1) | _BV(TXP0), _BV(TXREQ) | priority);

  // decrement id, skipping invalid addresses
  id -= (id % 4) ? 1 : 2;

  return true;
}


// read the highest priority message available and clear its buffer
bool CAN::receive(msg &message) {
  uint8_t address;

  // buffer 0 has the higher priority
  uint8_t status = readStatus(SPI_RX_STATUS);
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

  // standard id
  message.id = spiread() << 3;
  uint8_t sidl = spiread();
  message.id |= sidl >> 5;

  // skip extended id
  spiread();
  spiread();

  // DLC
  uint8_t length = spiread() & 0x0f;
  message.header.length = length;
  message.header.rtr = bit_is_set(sidl, SRR);

  // data
  if (!message.header.rtr) {
    for (uint8_t t = 0; t < length; t++) {
      message.data[t] = spiread();
    }
  }

  fastDigitalWrite(MCP2515_CS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  return true;
}


// change the operating mode of the can chip
void CAN::setMode(Mode mode) {
  // set transceiver mode
  modifyRegister(BFPCTRL, _BV(B1BFS), mode ? _BV(B1BFS) : 0x00);

  switch (mode) {
    case McuSleep:
      // disable low priority RX interrupt
      modifyRegister(CANINTE, _BV(RX1IE), 0x00);
      return;
    case Sleep:
      // enable wake interrupt
      modifyRegister(CANINTE, _BV(WAKIE), _BV(WAKIE));
      break;
    case Normal:
      // enable low priority RX and disable wake interrupts
      modifyRegister(CANINTE, _BV(WAKIE) | _BV(RX1IE), _BV(RX1IE));
      modifyRegister(CANINTF, _BV(WAKIF), 0x00);
      break;
  }

  // set controller mode
  modifyRegister(CANCTRL, _BV(REQOP2) | _BV(REQOP1) | _BV(REQOP0), mode);

  // wait until controller mode has been changed
  while ((readRegister(CANSTAT) & (_BV(REQOP2) | _BV(REQOP1) | _BV(REQOP0))) != mode);
}


// enable/disable standard id filtering
// high: NULL for no filter or an array in PROGMEM containing 2 id's followed by a mask
// low: NULL for no filter or an array in PROGMEM containing 4 id's followed by a mask
void CAN::setFilters(const uint16_t high[] PROGMEM, const uint16_t low[] PROGMEM) {
  uint16_t filter;
  uint8_t flags;

  // RXB0: enable/disable filters
  if (high) {
    // set high priority filter id's
    filter = pgm_read_word_near(high + 0);
    writeRegister(RXF0SIDH, filter >> 3);
    writeRegister(RXF0SIDL, filter << 5);

    filter = pgm_read_word_near(high + 1);
    writeRegister(RXF1SIDH, filter >> 3);
    writeRegister(RXF1SIDL, filter << 5);

    // set high pirority bit mask
    filter = pgm_read_word_near(high + 2);
    writeRegister(RXM0SIDH, filter >> 3);
    writeRegister(RXM0SIDL, filter << 5);

    // accept only standard id's that match the filters
    flags = _BV(RXM0);
  } else {
    // accept all messages
    flags = _BV(RXM1) | _BV(RXM0);
  }
  modifyRegister(RXB0CTRL, _BV(RXM1) | _BV(RXM0), flags);

  // RXB1: enable/disable filters
  if (low) {
    // set low priority filter id's
    filter = pgm_read_word_near(low + 0);
    writeRegister(RXF2SIDH, filter >> 3);
    writeRegister(RXF2SIDL, filter << 5);

    filter = pgm_read_word_near(low + 1);
    writeRegister(RXF3SIDH, filter >> 3);
    writeRegister(RXF3SIDL, filter << 5);

    filter = pgm_read_word_near(low + 2);
    writeRegister(RXF4SIDH, filter >> 3);
    writeRegister(RXF4SIDL, filter << 5);

    filter = pgm_read_word_near(low + 3);
    writeRegister(RXF5SIDH, filter >> 3);
    writeRegister(RXF5SIDL, filter << 5);

    // set low priority bit mask
    filter = pgm_read_word_near(low + 4);
    writeRegister(RXM1SIDH, filter >> 3);
    writeRegister(RXM1SIDL, filter << 5);

    // accept only standard id's that match the filters
    flags = _BV(RXM0);
  } else {
    // accept all messages
    flags = _BV(RXM1) | _BV(RXM0);
  }
  modifyRegister(RXB1CTRL, _BV(RXM1) | _BV(RXM0), flags);
}


void CAN::writeRegister( uint8_t address, uint8_t data ) {
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


uint8_t CAN::readStatus(uint8_t type) {
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


void CAN::modifyRegister(uint8_t address, uint8_t mask, uint8_t data) {
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


uint8_t CAN::readRegister(uint8_t address) {
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


