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

#define SD_CS         4    // SD card SPI select pin (output)
#define NUM_PRESETS   6

// filesystem stuff
#define UNKNOWN       -1
#define MAX_DEPTH     3

class Player : private VS1053
{
  public:
    void setup();
    void begin();
    void end();

    void play();
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

    uint16_t current;
    volatile uint16_t next;
    volatile bool shuffled;
    uint16_t presets[NUM_PRESETS];
    uint8_t repeatCount;

    // display stuff
    void updateText();
    struct {
      char text[23];
      volatile int8_t tag;
    } display;

    // filesystem stuff
    struct {
      File h;
      uint16_t folder;
      uint16_t first;
      uint16_t last;
    } path[MAX_DEPTH + 1];
    int8_t depth;

    // random stuff
    uint16_t seed;
    uint16_t xorshift(uint16_t min, uint16_t max);
};

#endif // Player.h
