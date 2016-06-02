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
  ibus.begin(47, high_filters, low_filters);

  // use IRQ for incoming messages
#ifdef SPI_HAS_TRANSACTION
  SPI.usingInterrupt(MCP2515_INT);
#endif
  attachInterrupt(MCP2515_INT, processMessage, LOW);

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
  ibus.setMode(CAN::McuSleep);
  sleep_mode();
  ibus.setMode(CAN::Normal);

  cdc.play();
}


// interrupt handler for incoming message
void processMessage() {
  CAN::msg msg;

  // if there's a message available
  if (ibus.receive(msg)) {
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
      cdc.begin();
      break;
    case 0x08: // power off
      msg.data[3] = 0x19;
      cdc.end();
      break;
  }
  msg.data[0] = 0x32;
  msg.data[6] = 0x00;
  while (!ibus.send(msg));

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
    while (!ibus.send(msg));
  }
}


inline __attribute__((always_inline))
void controlRequest(CAN::msg &msg) {
  // reply id
  msg.id = TX_CDC_CONTROL;

  // map CDC commands to methods
  switch (msg.data[1]) {
    case 0x00: // no command
      cdc.normal();
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
  while (!ibus.send(msg));
}


inline __attribute__((always_inline))
void displayRequest(CAN::msg &msg) {
  char *text;
  int8_t wanted = cdc.getText(text);

  if (msg.data[0] == 0x00) {
    if (wanted) {
      // check display owner
      switch (msg.data[1]) {
        case 0x12: // iSaab
          // send text
          msg.id = TX_SID_TEXT;
          msg.data[1] = 0x96;

          for (int8_t id = 5, i = 0; id >= 0; id--) {
            msg.data[0] = id;
            if (id == 5) msg.data[0] |= 0x40;

            msg.data[2] = (id < 3) ? 2 : 1;
            if (wanted < 0) msg.data[2] |= 0x80;

            // copy text
            msg.data[3] = text[i++];
            msg.data[4] = text[i++];
            if (id % 3) {
              msg.data[5] = text[i++];
              msg.data[6] = text[i++];
              msg.data[7] = text[i++];
            } else {
              msg.data[5] = 0x00;
              msg.data[6] = 0x00;
              msg.data[7] = 0x00;
            }
            while (!ibus.send(msg));
          }

          msg.data[2] = 0x05; // keep
          break;
        case 0xff: // available
          msg.data[2] = 0x03; // request
          break;
        default: // taken
          msg.data[2] = 0xff; // decline
          break;
      }
    } else {
      msg.data[2] = 0xff; // decline
    }

    // display request
    msg.id = TX_SID_REQUEST;
    msg.data[0] = 0x1f; // device
    msg.data[1] = 0x00; // row
    msg.data[3] = 0x12; // priority
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;
    while (!ibus.send(msg));
  }
}

