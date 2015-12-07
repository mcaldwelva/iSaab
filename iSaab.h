/*
 * iSaab -- A Virtual CD Changer for early 9-3's and 9-5's
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


// only accept these messages
const uint16_t ibus_filters[] PROGMEM =
  {RX_CDC_PRESENT, RX_CDC_CONTROL, 0x000, 0x000, 0x000, 0x000};
const uint16_t ibus_masks[] PROGMEM =
  {0x7ff, 0x7ff};


// prototypes
void processMessage();
void presenceRequest(CAN::msg &);
void controlRequest(CAN::msg &);

#endif // iSaab_H
