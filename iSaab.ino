/*
 * iSaab -- A Virtual CD Changer for early 9-3's and 9-5's
 *
 * 08/24/2015 Mike C. - v 1.0
 */

#include <SPI.h>
#include <SD.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <util/atomic.h>
#include "CAN.h"
#include "Player.h"
#include "iSaab.h"

CAN ibus;
Player cdc;

// one-time setup
void setup() {
  // setup sound card
  cdc.setup();

  // open I-Bus @ 47.619Kbps
  ibus.begin(47);
  ibus.setFilters(ibus_filters, ibus_masks);

  // use IRQ for incoming messages
#ifdef SPI_HAS_TRANSACTION
  SPI.usingInterrupt(MCP2515_IRQ);
#endif
  attachInterrupt(MCP2515_IRQ, processMessage, LOW);

  // save power
  power_adc_disable();
  power_twi_disable();
  power_usart0_disable();
  power_timer1_disable();
  power_timer2_disable();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}


// main loop
void loop() {
  cdc.loop();
  sleep_mode();
}


// send a new message out
void sendMessage(uint16_t id, const uint8_t data[]) {
  CAN::msg msg;

  msg.id = id;
  msg.header.rtr = 0;
  msg.header.length = IBUS_PACKET_SIZE;
  memcpy(msg.data, data, IBUS_PACKET_SIZE);

  while (ibus.send(msg) == 0xff);
}


// interrupt handler for incoming message
void processMessage() {
  CAN::msg msg;

  // if there's a message waiting
  while (ibus.available()) {
    // get it
    ibus.receive(msg);

    // act on it
    switch (msg.id) {
      case RX_CDC_PRESENT:
        presenceRequest(msg);
        break;

      case RX_CDC_CONTROL:
        controlRequest(msg);
        break;
    }
  }
}


inline __attribute__((always_inline))
void presenceRequest(CAN::msg &msg) {
  uint8_t action = msg.data[3] & 0x0f;

  // reply id
  msg.id = TX_CDC_PRESENT;

  // handle presence request
  switch (action) {
    case 0x02: // active
      msg.data[3] = 0x16;
      break;
    case 0x03: // power on
      cdc.begin();
      msg.data[3] = 0x03;
      break;
    case 0x08: // power off
      cdc.end();
      msg.data[3] = 0x19;
      break;
  }
  msg.data[0] = 0x32;
  msg.data[6] = 0x00;
  while (ibus.send(msg) == 0xff);

  // send rest of sequence
  switch (action) {
    case 0x02: // active
      msg.data[3] = 0x36;
      break;
    case 0x03: // power on
      msg.data[3] = 0x22;
      break;
    case 0x08: // power off
      msg.data[3] = 0x38;
      break;
  }
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  while (msg.data[0] < 0x62) {
    msg.data[0] += 0x10;
    while (ibus.send(msg) == 0xff);
  }
}


inline __attribute__((always_inline))
void controlRequest(CAN::msg &msg) {
  // reply id
  msg.id = TX_CDC_CONTROL;

  // map CDC commands to methods
  switch (msg.data[1]) {
    case 0x00: // no command
      break;
    case 0x14: // deselect CDC
    case 0xb0: // MUTE on
      cdc.pause();
      break;
    case 0x24: // select CDC
    case 0xb1: // MUTE off
      cdc.resume();
      break;
    case 0x35: // TRACK >>
      cdc.nextTrack();
      break;
    case 0x36: // TRACK <<
      cdc.prevTrack();
      break;
    case 0x45: // PLAY >>
      cdc.forward();
      break;
    case 0x46: // PLAY <<
      cdc.rewind();
      break;
    case 0x59: // NXT
      cdc.nextDisc();
      break;
    case 0x68: // 1 - 6
      cdc.preset(msg.data[2]);
      break;
    case 0x76: // RDM toggle
      if (msg.data[0] & 0x80) cdc.shuffle();
      break;
  }

  // get current status
  cdc.getStatus(msg.data);
  while (ibus.send(msg) == 0xff);
}
