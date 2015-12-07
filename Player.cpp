/*
 *  Player - Implements the essential features of a media player
 *           returns player status as a Saab-ready message
 *
 *  07/04/2015 Mike C. - v1.0
 */

#include <SD.h>
#include <util/atomic.h>
#include "Player.h"

// guarantees progress through the entire library
// and next track selected in < 2 seconds
#define RANDOM_TRACK (trackNum + 1 + random(512))

Player::Player() {
  // initial player state
  state = Off;
  depth = 0;

  // select the first track
  trackNum = 0;
  trackNext = 0;

  // no music files in the root
  path[0].min = 0;
  path[0].max = 0;
  path[0].folder = 0;
}


// perform one-time setup on power-up
void Player::setup() {
  // deselect SD card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // setup VS1053 pins
  VS1053::setup();

  // initialize shuffler
  unsigned long seed = 0;
  for (int i = 0; i < 32; i++) {
    seed <<= 1;
    seed |= analogRead(8) & 1;
  }
  randomSeed(seed);
}


// start-up player
bool Player::begin() {
#if (DEBUGMODE==1)
  Serial.println(F("BEGIN"));
#endif
  if (state == Off) {
    // turn on sound card
    if (!VS1053::begin()) {
#if (DEBUGMODE==1)
      Serial.println(F("Failed to initialize vs1053!"));
#endif
      return false;
    }

    // initialize card reader
    if (!SD.begin(SD_CS)) {
      // this will return an error after the first call
      // but we need it here so we can change the card
    }

    // open SD root
    path[0].dir = SD.open(F("/"));

    // load FLAC patch
    loadPlugin(F("PATCH053.BIN"));

    // read presets
    readPresets(F("PRESETS.TXT"));

    state = Standby;
  }

  return true;
}

// shut-down player
void Player::end() {
#if (DEBUGMODE==1)
  Serial.println(F("END"));
#endif
  if (state != Off) {
    // resume current track on start-up
    trackNext = trackNum;

    // collapse path structure
    stopTrack();
    while (depth > 0) {
      path[depth].dir.close();
      depth--;
    }

    // close root
    path[0].dir.close();

    // turn off sound card
    VS1053::end();

    state = Off;
  }
}

// main playback loop
void Player::loop() {
#if (DEBUGMODE==1)
  if
#else
  while
#endif
  (state != Off) {
    if (currentTrack) {
      // keep the audio buffer full
      playTrack();
    } else {
#if (DEBUGMODE==1)
      Serial.println(F("loop: nothing playing"));
#endif

      // get the next track if one hasn't already been selected
      if (trackNext == UNKNOWN) {
        nextTrack();
      }

      openNextTrack();
      startTrack();
    }
  }
}


void Player::standby() {
#if (DEBUGMODE==1)
  Serial.println(F("STANDBY"));
#endif
  if (state != Off) state = Standby;
}


void Player::pause() {
#if (DEBUGMODE==1)
  Serial.println(F("PAUSE"));
#endif
  if (state == Playing) state = Paused;
}


void Player::resume() {
#if (DEBUGMODE==1)
  Serial.println(F("RESUME"));
#endif
  if (state != Off) state = Playing;
}


void Player::shuffle() {
  shuffled = !shuffled;
#if (DEBUGMODE==1)
  Serial.print(F("SHUFFLE "));
  Serial.println(shuffled ? F("ON") : F("OFF"));
#endif
}


void Player::nextTrack() {
#if (DEBUGMODE==1)
  Serial.println(F("NEXT"));
#endif

  if (shuffled) {
    trackNext = RANDOM_TRACK;
  } else {
    // if no track is queued up
    if (trackNext == UNKNOWN) {
      trackNext = trackNum + 1;
    } else {
      // keep skipping
      trackNext++;
    }
  }

  stopTrack();
}


void Player::prevTrack() {
#if (DEBUGMODE==1)
  Serial.println(F("PREVIOUS"));
#endif

  if (trackTime()) {
    // start this track over again
    trackNext = trackNum;
  } else {
    if (shuffled) {
      trackNext = RANDOM_TRACK;
    } else {
      // if no track is queued up
      if (trackNext == UNKNOWN) {
        trackNext = trackNum - 1;
      } else {
        // keep skipping
        trackNext--;
      }
    }
  }

  stopTrack();
}


void Player::nextDisc() {
#if (DEBUGMODE==1)
  Serial.println(F("NEXT DISC"));
#endif

  if (shuffled) {
    trackNext = RANDOM_TRACK;
  } else {
    trackNext = path[depth].max;
  }

  stopTrack();
}


// read presets from file
void Player::readPresets(const __FlashStringHelper* fileName) {
  // clear existing presets
  memset(presets, 0, sizeof(trackNum) * NUM_PRESETS);

  // open the file
  File file = SD.open(fileName);
  if (file) {
    short i = 0;
    while (i < NUM_PRESETS && file.available()) {
      uint8_t c = file.read() - '0';
      if (c <= 9) {
        presets[i] *= 10;
        presets[i] += c;
      } else {
        i++;
      }
    }

    // done
    file.close();
  }
}


void Player::preset(uint8_t memory) {
#if (DEBUGMODE==1)
  Serial.print(F("PRESET: "));
  Serial.println(memory, DEC);
#endif

  trackNext = presets[memory - 1];
  stopTrack();
}


void Player::rewind() {
#if (DEBUGMODE==1)
  Serial.println(F("REWIND"));
#endif

  skip(-3);
}


void Player::forward() {
#if (DEBUGMODE==1)
  Serial.println(F("FAST FORWARD"));
#endif

  skip(+1);
}


// gather player status for Saab
void Player::getStatus(uint8_t data[]) {
  // if player is active
  if (state >= Playing) {
    uint16_t time;
    uint8_t track;
    uint8_t disc;

    // gather track information
    if (trackNext == UNKNOWN) {
      // playing current track
      time = trackTime();
      track = trackNum - path[depth].min + 1;
      disc = path[depth].folder;
    }
    else if (trackNext >= path[depth].max) {
      // new track is on the next disc
      time = 0;
      track = trackNext - path[depth].max + 1;
      disc = path[depth].folder + 1;
    }
    else if (trackNext < path[depth].min) {
      // new track is on the previous disc
      time = 0;
      track = 100 - (trackNum - trackNext);
      disc = path[depth].folder - 1;
    }
    else {
      // new track is on the current disc
      time = 0;
      track = trackNext - path[depth].min + 1;
      disc = path[depth].folder;
    }

    // seconds
    uint8_t sec = time % 60;
    data[6] = sec / 10;
    data[6] <<= 4;
    data[6] |= sec % 10;

    // minutes
    uint8_t min = time / 60;
    data[5] = (min / 10) % 10;
    data[5] <<= 4;
    data[5] |= min % 10;

    // track
    data[4] = (track / 10) % 10;
    data[4] <<= 4;
    data[4] |= track % 10;

    // play status
    bool rapid = (data[1] == 0x45) || (data[1] == 0x46);
    data[3] = rapid ? Rapid : Playing;

    // disc
    data[3] |= disc % 9 + 1;
  } else {
    data[6] = 0xff;
    data[5] = 0xff;
    data[4] = 0xff;
    data[3] = state | 1;
  }

  // changed, ready, reply
  static uint8_t last[4] = {0x01, 0xff, 0xff, 0xff};
  data[0] = 0x20;
  if (data[1] || memcmp(last, (data + 3), sizeof(last))) {
    if ((last[0] & 0xf0) == Off) data[0] |= 0x40;
    data[0] |= 0x80;
    memcpy(last, (data + 3), sizeof(last));
  }

  // married, random
  data[7] = 0xd0;
  if (shuffled) data[7] |= 0x20;

  // full magazine
  data[2] = 0b00111111;
  data[1] = 0x00;
}


void Player::dumpPath() {
  Serial.println(F("dumpPath..."));

  for (int i = 0; i <= depth; i++) {
    Serial.print(i, DEC);
    Serial.print(":");
    Serial.print(path[i].dir.name());
    Serial.print(F(", min:"));
    Serial.print(path[i].min);
    Serial.print(F(", max:"));
    Serial.print(path[i].max);
    Serial.println();
  }

#if (DEBUGMODE==1)
  tags::getTags(currentTrack);
#else
  Serial.println(currentTrack.name());
#endif
}


// find the new track number on the file system
void Player::openNextTrack() {
  File entry;
  unsigned int file_count;
  unsigned int dir_count;

#if (DEBUGMODE==1)
  Serial.print(F("DISCOVER: "));
  Serial.println(trackNext, DEC);
#endif

  // collapse current path as necessary
  while (path[depth].min > trackNext) {
    path[depth].dir.close();
    depth--;
  }

top:
  if (trackNext > trackNum) {
    // start with the current file
    file_count = trackNum + 1;
  } else {
    // start at the top of the directory
    path[depth].dir.rewindDirectory();
    file_count = path[depth].min;
  }
  dir_count = path[depth].folder;

#if (DEBUGMODE==1)
  Serial.print(F("Starting at:"));
  Serial.println(file_count, DEC);
#endif

  // search from here until we find our file
  while (file_count <= trackNext) {
    if (state == Off) {
      // the rug's been pulled out from under us
      return;
    }
    entry = path[depth].dir.openNextFile();

    // explore this directory if we haven't already
    // or we've already explored this directory and we
    // know the file is in here
    if (file_count < path[depth].max) {
      while (entry) {
        if (!entry.isDirectory()) {
#if (DEBUGMODE==2)
          Serial.print(file_count);
          Serial.print(' ');
          Serial.println(entry.name());
#endif

          if (file_count++ == trackNext && path[depth].max != UNKNOWN) {
            // this is the file we're looking for
            goto done;
          } else {
            // skip this file
            entry.close();
          }
        } else {
          // skip this directory
          entry.close();
        }
        entry = path[depth].dir.openNextFile();
      }

      // we know how many files are in this dir now
      path[depth].max = file_count;
      path[depth].dir.rewindDirectory();

      // only count this folder if it has files
      if (path[depth].min != path[depth].max) dir_count++;

      // rollback the file count if we've discovered the new track
      if (file_count > trackNext) file_count = path[depth].min;
    } else {

      // start checking sub-directories
      while (entry && !entry.isDirectory()) {
        entry.close();
        entry = path[depth].dir.openNextFile();
      }

      // if we find a sub-dir, go in if we can
      if (entry && depth < MAX_DEPTH - 1) {
#if (DEBUGMODE==1)
        Serial.print(dir_count);
        Serial.print(' ');
        Serial.println(entry.name());
#endif

        // getStatus needs these to be consistent
        ATOMIC_BLOCK(ATOMIC_FORCEON) {
          depth++;
          path[depth].folder = dir_count;
          path[depth].min = file_count;
        }
        path[depth].dir = entry;
        path[depth].max = UNKNOWN;
      } else {
        // if there are no sub-dirs
        if (depth > 0) {
          // pop out
          path[depth].dir.close();
          depth--;
        } else {
          // we've searched the entire disk
#if (DEBUGMODE==1)
          Serial.print(dir_count, DEC);
          Serial.println(F(" dirs"));
          Serial.print(file_count, DEC);
          Serial.println(F(" files"));
#endif

          // go back to the beginning
          trackNext %= file_count;
          goto top;
        }
      }
    }
  }

done:
  currentTrack = entry;
#if (DEBUGMODE==1)
  dumpPath();
#endif

  // getStatus needs these to be consistent
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    trackNum = trackNext;
    trackNext = UNKNOWN;
  }
}
