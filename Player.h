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
    void on();
    void off();

    // operations
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

    // status
    State getState() { return state; };
    bool isShuffled() { return shuffled; };
    uint16_t getTime() { return trackTime(); };

    uint8_t getTrack() {
      uint8_t ret;

      if (next == UNKNOWN) {
        // track is on the current disc
        ret = current - path[depth].first;
      } else if (next >= path[depth].last) {
        // new track is on the next disc
        ret = next - path[depth].last;
      } else if (next < path[depth].first) {
        // new track is on the previous disc
        ret = 99 - (current - next);
      } else {
        // new track is on the current disc
        ret = next - path[depth].first;
      }

      return ret;
    };

    uint8_t getDisc() {
      uint8_t ret;

      if (next == UNKNOWN) {
        // track is on the current disc
        ret = path[depth].folder;
      } else if (next >= path[depth].last) {
        // new track is on the next disc
        ret = path[depth].folder + 1;
      } else if (next < path[depth].first) {
        // new track is on the previous disc
        ret = path[depth].folder - 1;
      } else {
        // new track is on the current disc
        ret = path[depth].folder;
      }

      return ret;
    };

    // display
    void nextText();
    void text(uint8_t id);
    bool getText(char dst[MAX_TAG_LENGTH]);

  private:
    void begin();
    void end();
    void openTrack();
    void readPresets(const __FlashStringHelper* fileName);

    uint16_t current;
    volatile uint16_t next;
    volatile bool shuffled;
    uint16_t presets[NUM_PRESETS];
    uint8_t repeatCount;

    // display stuff
    uint8_t tag;
    volatile bool updated;

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
