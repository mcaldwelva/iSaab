/*
 * iSaab -- A Virtual CD Changer for early 9-3's and 9-5's
 *
 * This is designed to work on the BlueSaab 3.5mm module described here:
 * http://bluesaab.blogspot.com/2014/03/how-to-build-your-own-35mm-version-of.html
 *
 * In combination with a VS1053 and SD card, such as:
 * http://www.geeetech.com/wiki/index.php/VS1053_MP3_breakout_board_with_SD_card
 *
 * 08/24/2015 Mike C. - v 1.0
 */

#ifndef iSaab_H
#define iSaab_H

#define IBUS_PACKET_SIZE         8

/**
 * CDC presence
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#6A1
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#6A2
 */
#define RX_CDC_PRESENT           0x6a1
#define TX_CDC_PRESENT           0x6a2



/**
 * CDC control
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#3C0
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#3C8
 */
#define RX_CDC_CONTROL           0x3c0
#define TX_CDC_CONTROL           0x3c8


/**
 * SID messages
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#368
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#328
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#348
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#430
 */
#define TX_SID_TEXT              0x328
#define TX_SID_PRIORITY          0x348
#define RX_SID_PRIORITY          0x368
#define TX_SID_BEEP              0x430
const uint8_t beep_msg[] PROGMEM =
  {0x80, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


// only accept these messages
const uint16_t ibus_filters[] PROGMEM =
  {RX_CDC_CONTROL, 0x000, RX_CDC_PRESENT, 0x000, 0x000, 0x000};
const uint16_t ibus_masks[] PROGMEM =
  {0x7ff, 0x7ff};


// prototypes
void processMessage();
void presenceRequest(uint8_t data[]);
void controlRequest(uint8_t data[]);
void sendMessage(uint16_t, const uint8_t data[]);

#endif // iSaab_H
