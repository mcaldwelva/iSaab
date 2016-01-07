/*
 *  Player - Implements the essential features of a media player
 *           returns player status as a Saab-ready message
 *
 *  07/04/2015 Mike C. - v1.0
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <SD.h>
#include "VS1053.h"

#define DEBUGMODE     0
#define SD_CS         4    // SD card SPI select pin (output)
#define NUM_PRESETS   6

// filesystem stuff
#define UNKNOWN       -1
#define MAX_DEPTH     4

class Player : private VS1053
{
  public:
    Player();
    void setup();
    bool begin();
    void end();
    void loop();

    void standby();
    void pause();
    void resume();
    void shuffle();
    void rewind();
    void forward();
    void nextTrack();
    void prevTrack();
    void nextDisc();
    void preset(uint8_t);

    void getStatus(uint8_t[]);

  private:
    void openNextTrack();
    void readPresets(const __FlashStringHelper*);

    unsigned int trackNum;
    volatile unsigned int trackNext;
    volatile bool shuffled;
    unsigned int presets[NUM_PRESETS];
    unsigned short rapidCount;

    // filesystem stuff
    void dumpPath();
    struct {
      File dir;
      unsigned int folder;
      unsigned int min;
      unsigned int max;
    } path[MAX_DEPTH];
    short depth;
};

#endif


