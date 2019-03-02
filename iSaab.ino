/*
 * iSaab -- A virtual CD Changer for early 9-3's and 9-5's
 *
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
  SPI.usingInterrupt(MCP2515_INT);
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
  sleep_mode();
  cdc.play();
}


// interrupt handler for incoming message
void processMessage() {
  static uint8_t gap = 0;
  CAN::msg msg;

  if (gap == 0) {
    ibus.setMode(CAN::Normal);
  }

  // if there's a message available
  if (ibus.receive(msg)) {
    // act on it
    switch (msg.id) {
      case RX_CDC_CONTROL:
        controlRequest(msg);
        gap = 15;
        break;

      case RX_CDC_POWER:
        powerRequest(msg);
        gap = 15;
        break;

      case RX_SID_REQUEST:
        displayRequest(msg);
        gap--;
        break;
    }
  }

  if (gap == 0) {
    ibus.setMode(CAN::ListenOnly);
  }
}


inline __attribute__((always_inline))
void powerRequest(CAN::msg &msg) {
  uint8_t action = msg.data[3] & 0x0f;

  // reply id
  msg.id = TX_CDC_POWER;

  // handle power request
  switch (action) {
    case 0x02: // active
      msg.data[3] = 0x16;
      break;
    case 0x03: // power on
      msg.data[3] = 0x03;
      cdc.on();
      break;
    case 0x08: // power off
      msg.data[3] = 0x19;
      cdc.off();
      break;
  }
  msg.data[0] = 0x32;
  msg.data[1] = 0x00;
  msg.data[2] = 0x00;
  msg.data[4] = 0x01;
  msg.data[5] = 0x02;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;
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
      cdc.nextDisc();
      break;
    case 0x68: // 1 - 6
      cdc.preset(msg.data[2]);
      break;
    case 0x76: // RDM toggle
      cdc.shuffle();
      break;
    case 0xb0: // MUTE on
    case 0x14: // deselect CDC
    case 0x84: // SCAN disc
      cdc.pause();
      break;
    case 0xb1: // MUTE off
    case 0x24: // select CDC
    case 0x88: // SCAN magazine
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

  // check row
  if (msg.data[0] == 0x00) {
#if (DEBUGMODE>=1)
    uint8_t tec = ibus.getSendErrors();
    uint8_t rec = ibus.getReceiveErrors();
    uint8_t eflg = ibus.getErrorFlags();
    uint16_t mem = FreeRam();

    if (wanted == 13 || tec || rec || eflg) {
      text[0]  = 'T';
      text[3]  = (tec % 10) + '0'; tec /= 10;
      text[2]  = (tec % 10) + '0'; tec /= 10;
      text[1]  = (tec % 10) + '0';
      text[4]  = 'R';
      text[7] = (rec % 10) + '0'; rec /= 10;
      text[6] = (rec % 10) + '0'; rec /= 10;
      text[5]  = (rec % 10) + '0';
      text[8]  = 'M';
      text[11] = (mem % 10) + '0'; mem /= 10;
      text[10] = (mem % 10) + '0'; mem /= 10;
      text[9]  = (mem % 10) + '0';
      text[12] = 'O';
      text[13] = eflg & _BV(7) ? '1' : ' ';
      text[14] = eflg & _BV(6) ? '0' : ' ';
      text[15] = 'E';
      text[16] = eflg & _BV(5) ? 'b' : ' ';
      text[17] = eflg & _BV(4) ? 't' : ' ';
      text[18] = eflg & _BV(3) ? 'r' : ' ';
      text[19] = 'W';
      text[20] = eflg & _BV(2) ? 't' : ' ';
      text[21] = eflg & _BV(1) ? 'r' : ' ';
      text[22] = eflg & _BV(0) ? 'e' : ' ';
      text[23] = wanted = 13;
    }
#endif

    if (wanted) {
      // check owner
      switch (msg.data[1]) {
        case 0x12: // iSaab
          // send text
          msg.id = TX_SID_TEXT;
          msg.data[1] = 0x96;

          for (int8_t id = 5, i = 0; id >= 0; id--) {
            // sequence id, start of sequence flag
            msg.data[0] = id;
            if (id == 5) msg.data[0] |= 0x40;

            // row id, new text flag
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
    msg.data[3] = 0x12; // iSaab
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;
    while (!ibus.send(msg));
  }
}
