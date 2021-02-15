/*
 * iSaab -- A CD Changer replacement for Saab 9-3 OG and 9-5 OG
 *
 */

#include <SPI.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include "CAN.h"
#include "CDC.h"
#include "iSaab.h"

uint8_t tag = AudioFile::NUM_TAGS;
bool newText;

// one-time setup
void setup() {
  // setup sound card
  CDC.setup();

#ifndef SERIALMODE
  // open I-Bus @ 47.619Kbps
  CAN.begin(47, high_filters, low_filters);

  // use IRQ for incoming messages
  SPI.usingInterrupt(MCP2515_INT);
  attachInterrupt(MCP2515_INT, processMessage, LOW);

  // reduce power
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  power_usart0_disable();
  power_timer1_disable();
#else
  // open serial
  Serial.begin(115200);

  // use timer to read serial
  SPI.usingInterrupt(255);
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS12);
  OCR1A  = F_CPU / 256 / 2 - 1;
  TIMSK1 |= _BV(OCIE1A);

  // reduce power
  set_sleep_mode(SLEEP_MODE_IDLE);
#endif

  power_timer2_disable();
  power_adc_disable();
  power_twi_disable();
}


// main loop
void loop() {
  sleep_mode();
  CDC.loop();
}


// interrupt handler for incoming message
void processMessage() {
  static uint8_t gap = 0;

  // starting to receive CDC messages, wake up
  if (gap == 0) {
    CAN.setMode(CANClass::Normal);
  }

  // if there's a message available
  CANClass::msg msg;
  if (receiveMessage(msg)) {
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

  // no more CDC messages, go to sleep
  if (gap == 0) {
    CAN.setMode(CANClass::ListenOnly);
  }
}


void powerRequest(CANClass::msg &msg) {
  uint8_t action = msg.data[3] & 0x0f;

  // reply id
  msg.id = TX_CDC_POWER;

  // handle power request
  switch (action) {
    case 0x2: // active
      msg.data[3] = 0x16;
      break;
    case 0x3: // power on
      msg.data[3] = 0x03;
      CDC.on();
      break;
    case 0x8: // power off
      msg.data[3] = 0x19;
      CDC.off();
      break;
  }
  msg.data[0] = 0x32;
  msg.data[1] = 0x00;
  msg.data[2] = 0x00;
  msg.data[4] = 0x01;
  msg.data[5] = 0x02;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;
  sendMessage(msg);

  // send rest of sequence
  switch (action) {
    case 0x2: // active
      msg.data[3] = 0x36;
      break;
    case 0x3: // power on
      msg.data[3] = 0x22;
      break;
    case 0x8: // power off
      msg.data[3] = 0x38;
      break;
  }
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  while (msg.data[0] < 0x62) {
    msg.data[0] += 0x10;
    sendMessage(msg);
  }
}


void controlRequest(CANClass::msg &msg) {
  static int8_t repeatCount;

  // reply id
  msg.id = TX_CDC_CONTROL;

  // ready, reply
  msg.data[0] = 0x60;

  // map CDC commands to methods
  repeatCount++;
  switch (msg.data[1]) {
    case 0x00: // no command
      msg.data[0] = 0x20;
      repeatCount = 0;
      CDC.normal();
      break;
    case 0x35: // TRACK >>
      CDC.skipTrack(repeatCount);
      break;
    case 0x36: // TRACK <<
      CDC.skipTrack(1 - repeatCount);
      break;
    case 0x45: // PLAY >>
      CDC.skipTime(repeatCount);
      break;
    case 0x46: // PLAY <<
      CDC.skipTime(-repeatCount);
      break;
    case 0x59: // NXT
      if (repeatCount == 1) {
        if (CDC.isShuffled()) {
          tag = (tag + 1) % (AudioFile::NUM_TAGS + 1);
          newText = true;
        } else {
          CDC.nextDisc();
        }
      }
      break;
    case 0x68: // 1 - 6
      if (repeatCount == 1) {
        uint8_t select = msg.data[2] - 1;
        if (CDC.isShuffled()) {
          tag = (select == tag) ? AudioFile::NUM_TAGS : select;
          newText = true;
        } else {
          CDC.preset(select);
        }
      }
      break;
    case 0x76: // RDM toggle
      if (repeatCount == 1) {
        msg.data[0] |= 0x80;
        CDC.shuffle();
      }
      break;
    case 0xb0: // MUTE on
    case 0x14: // deselect CDC
    case 0x84: // SCAN disc
      CDC.pause();
      break;
    case 0xb1: // MUTE off
    case 0x24: // select CDC
    case 0x88: // SCAN magazine
      CDC.resume();
      break;
  }

  msg.data[1] = 0x00;

  // magazine
  msg.data[2] = 0b00111111;

  // play status, disc
  msg.data[3] = CDC.getState() & 0xf0;
  msg.data[3] |= CDC.getDisc() % 6 + 1;

  // track
  uint8_t track = CDC.getTrack() + 1;
  msg.data[4] = (track / 10) % 10;
  msg.data[4] <<= 4;
  msg.data[4] |= track % 10;

  // minutes
  uint16_t time = CDC.getTime();
  uint8_t min = time / 60;
  msg.data[5] = (min / 10) % 10;
  msg.data[5] <<= 4;
  msg.data[5] |= min % 10;

  // seconds
  uint8_t sec = time % 60;
  msg.data[6] = sec / 10;
  msg.data[6] <<= 4;
  msg.data[6] |= sec % 10;

  // married, RDM
  msg.data[7] = 0xd0;
  if (CDC.isShuffled()) msg.data[7] |= 0x20;

  // flag disc, track, or time change
  static uint8_t last[4] = {0x02, 0x01, 0x00, 0x00};
  if (memcmp(last, (msg.data + 3), sizeof(last))) {
    msg.data[0] |= 0x80;
    if (time == 0) newText = true;
    memcpy(last, (msg.data + 3), sizeof(last));
  }

  sendMessage(msg);
}


void displayRequest(CANClass::msg &msg) {
  // check row
  if (msg.data[0] == 0x00) {
    const String text = CDC.getText(tag);
    if ((CDC.getState() == VS1053::Playing) && text.length()) {
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
            if (newText) msg.data[2] |= 0x80;

            // copy text
            switch (id) {
              default:
                msg.data[3] = text[i++];
                msg.data[4] = text[i++];
                msg.data[5] = text[i++];
                msg.data[6] = text[i++];
                msg.data[7] = text[i++];
                break;
              case 3:
                msg.data[3] = text[i++];
                msg.data[4] = text[i++];
                if (text[i] == ' ') i++;
                msg.data[5] = 0x00;
                msg.data[6] = 0x00;
                msg.data[7] = 0x00;
                break;
              case 0:
                msg.data[3] = text[i++];
                msg.data[4] = tag + 1;
                msg.data[5] = 0x00;
                msg.data[6] = 0x00;
                msg.data[7] = 0x00;
                break;
            }
            sendMessage(msg);
          }

          newText = false;
          msg.data[2] = 0x05; // keep
          break;
        case 0xff: // available
          newText = true;
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
    sendMessage(msg);
  }
}


void sendMessage(const CANClass::msg &msg) {
#ifndef SERIALMODE
  while (!CAN.send(msg));
#else
  Serial.print(msg.id, HEX);
  Serial.print(' ');

  for (uint8_t i = 0; i < 8; i++) {
    Serial.print(msg.data[i], HEX);
    Serial.print(' ');
  }

  Serial.println();
#endif
}


bool receiveMessage(CANClass::msg &msg) {
#ifndef SERIALMODE
  return CAN.receive(msg);
#else
  msg.id = RX_CDC_CONTROL;

  if (Serial.available()) {
    char c = Serial.read();

    switch (c) {
      case 'b':
        msg.id = RX_CDC_POWER;
        msg.data[3] = 0x03;
        break;
      case 'e':
        msg.id = RX_CDC_POWER;
        msg.data[3] = 0x08;
        break;
      case 't':
        msg.id = RX_SID_REQUEST;
        msg.data[0] = 0x00;
        msg.data[1] = 0x12;
        break;
      case 'p':
        msg.data[1] = 0xb0;
        break;
      case 'r':
        msg.data[1] = 0xb1;
        break;
      case '*':
        msg.data[1] = 0x76;
        break;
      case '>':
        msg.data[1] = 0x35;
        break;
      case '<':
        msg.data[1] = 0x36;
        break;
      case 'n':
        msg.data[1] = 0x59;
        break;
      case '-':
        msg.data[1] = 0x46;
        break;
      case '+':
        msg.data[1] = 0x45;
        break;
      case '1': case '2': case '3': case '4': case '5': case '6':
        msg.data[1] = 0x68;
        msg.data[2] = c - '0';
        break;
    }
  } else {
    msg.data[1] = 0x00;
  }

  return true;
#endif
}


#ifdef SERIALMODE
ISR(TIMER1_COMPA_vect) { processMessage(); }
#endif
