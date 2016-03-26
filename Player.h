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
#define MAX_FILES     4095
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
    void normal();
    void nextTrack();
    void prevTrack();
    void nextDisc();
    void preset(uint8_t memory);

    void getStatus(uint8_t data[]);
    int getText(char *&buf);

  private:
    void openNextTrack();
    void readPresets(const __FlashStringHelper* fileName);

    uint16_t trackNum;
    volatile uint16_t trackNext;
    volatile bool shuffled;
    uint16_t presets[NUM_PRESETS];
    uint8_t rapidCount;

    // display stuff
    void updateText();
    struct {
      char text[23];
      volatile int8_t tag;
    } display;

    // filesystem stuff
    void dumpPath();
    struct {
      File h;
      uint16_t iFile;
      uint16_t iFolder;
      uint16_t nFiles : 12;
      bool hasFolders : 1;
    } path[MAX_DEPTH];
    int8_t depth;
};

#endif // Player.h

