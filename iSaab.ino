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


// interrupt handler for incoming message
void processMessage() {
  CAN::msg msg;

  // if there's a message waiting
  while (ibus.available()) {
    // get it
    ibus.receive(msg);

    // act on it
    switch (msg.id) {
      case RX_CDC_CONTROL:
        controlRequest(msg);
        break;

      case RX_CDC_PRESENT:
        presenceRequest(msg);
        break;

      case RX_SID_PRIORITY:
        displayRequest(msg);
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
      msg.data[3] = 0x03;
      ibus.setLowPriorityInterrupts(true);
      cdc.begin();
      break;
    case 0x08: // power off
      msg.data[3] = 0x19;
      cdc.end();
      ibus.setLowPriorityInterrupts(false);
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
      if (msg.data[0] & 0x80) cdc.nextDisc();
      break;
    case 0x68: // 1 - 6
      if (msg.data[0] & 0x80) cdc.preset(msg.data[2]);
      break;
    case 0x76: // RDM toggle
      if (msg.data[0] & 0x80) cdc.shuffle();
      break;
    case 0xb0: // MUTE on
      cdc.pause();
      break;
    case 0x14: // deselect CDC
      cdc.standby();
      break;
    case 0xb1: // MUTE off
    case 0x24: // select CDC
      cdc.resume();
      break;
  }

  // get current status
  cdc.getStatus(msg.data);
  while (ibus.send(msg) == 0xff);
}


inline __attribute__((always_inline))
void displayRequest(CAN::msg &msg) {
  char *text = cdc.getText();

  if (msg.data[0] == 0x00) {
    if (text[0]) {
      if (msg.data[1] == 0x0e) {
        // send text
        msg.id = TX_SID_TEXT;
        msg.data[1] = 0x96;

        // repeat a new message
//        for (uint8_t n = (text[23] & 0x80) ? 1 : 0; n < 2; n++)
        for (int8_t id = 5, i = 0; id >= 0 ; id--) {
          msg.data[0] = id;
          if (id == 5) msg.data[0] |= 0x40;

          msg.data[2] = (id < 3) ? 2 : 1;
//          if (n == 0) msg.data[2] |= 0x80;
          if (text[23] & 0x80) msg.data[2] |= 0x80;

          // copy text
          msg.data[3] = text[i++];
          msg.data[4] = text[i++];
          if (id == 0) msg.data[4] &= 0x7f;
          if (id % 3) {
            msg.data[5] = text[i++];
            msg.data[6] = text[i++];
            msg.data[7] = text[i++];
          } else {
            msg.data[5] = 0x20;
            msg.data[6] = 0x20;
            msg.data[7] = 0x20;
          }
          while (ibus.send(msg) == 0xff);
        }

        // keep display
        msg.data[2] = 0x05;
      } else {
        // want display
        msg.data[2] = 0x03;
      }
    } else {
      // no display
      msg.data[2] = 0xff;
    }

    // display request
    msg.id = TX_SID_REQUEST;
    msg.data[0] = 0x1f; // device
    msg.data[1] = 0x00; // row
    msg.data[3] = 0x0e; // priority
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;
    while (ibus.send(msg) == 0xff);
  }
}

