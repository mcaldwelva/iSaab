/*
 * iSaab -- A Virtual CD Changer for early 9-3's and 9-5's
 *
 * 08/24/2015 Mike C. - v 1.0
 */

#ifndef iSaab_H
#define iSaab_H

/**
 * CDC presence
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#6A1
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#6A2
 */
#define RX_CDC_POWER             0x6a1
#define TX_CDC_POWER             0x6a2


/**
 * CDC control
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#3C0
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#3C8
 */
#define RX_CDC_CONTROL           0x3c0
#define TX_CDC_CONTROL           0x3c8


/**
 * CDC display
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#337
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#357
 * http://pikkupossu.1g.fi/tomi/projects/i-bus/i-bus.html#368
 */
#define RX_SID_REQUEST           0x368
#define TX_SID_REQUEST           0x357
#define TX_SID_TEXT              0x337


// only accept these messages
const uint16_t high_filters[] PROGMEM = {RX_CDC_POWER, RX_CDC_CONTROL, 0x7ff};
const uint16_t low_filters[] PROGMEM = {RX_SID_REQUEST, 0x000, 0x000, 0x000, 0x7ff};

#endif // iSaab_H
