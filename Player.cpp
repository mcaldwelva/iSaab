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
  rapidCount = 0;
  display.tag = 0;

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
  uint32_t seed = 0;
  for (uint8_t i = 0; i < 32; i++) {
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

    state = Playing;
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

    // close any open file
    stopTrack();
    updateText();
    
    // collapse path structure
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
  while (state != Off) {
    if (currentTrack) {
      // keep the audio buffer full
      playTrack();
    } else {
#if (DEBUGMODE==1)
      Serial.println(F("loop: nothing playing"));
#endif
      updateText();

      // get the next track if one hasn't already been selected
      if (trackNext == UNKNOWN) {
        nextTrack();
      }

      openNextTrack();
      startTrack();
      updateText();
    }
  }
}


void Player::standby() {
#if (DEBUGMODE==1)
  Serial.println(F("STANDBY"));
#endif
  if (state != Off) {
    state = Standby;
    updateText();
  }
}


void Player::pause() {
#if (DEBUGMODE==1)
  Serial.println(F("PAUSE"));
#endif
  if (state == Playing) {
    state = Paused;
    updateText();
  }
}


void Player::resume() {
#if (DEBUGMODE==1)
  Serial.println(F("RESUME"));
#endif
  if (state != Off) {
    state = Playing;
    updateText();
  }
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
    display.tag = (abs(display.tag) + 1) % (NUM_PRESETS + 1);
    updateText();
  } else {
    trackNext = path[depth].max;
    stopTrack();
  }
}


// read presets from file
void Player::readPresets(const __FlashStringHelper* fileName) {
  // clear existing presets
  memset(presets, 0, sizeof(trackNum) * NUM_PRESETS);

  // open the file
  File file = SD.open(fileName);
  if (file) {
    uint8_t i = 0;
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

  if (shuffled) {
    if (memory == abs(display.tag)) {
      display.tag = 0;
    } else {
      display.tag = memory;
    }
    updateText();
  } else {
    trackNext = presets[memory - 1];
    stopTrack();
  }
}


void Player::rewind() {
#if (DEBUGMODE==1)
  Serial.println(F("REWIND"));
#endif
  int8_t seconds;

  if (rapidCount >= 15) {
    seconds = -12;
  } else if (rapidCount >= 10) {
    seconds = -7;
  } else {
    seconds = -3;
  }

  skip(seconds);
}


void Player::forward() {
#if (DEBUGMODE==1)
  Serial.println(F("FAST FORWARD"));
#endif
  int8_t seconds;

  if (rapidCount >= 15) {
    seconds = +10;
  } else if (rapidCount >= 10) {
    seconds = +5;
  } else {
    seconds = +1;
  }

  skip(seconds);
}


// gather player status for Saab
void Player::getStatus(uint8_t data[]) {
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

  // married, random
  data[7] = 0xd0;
  if (shuffled) data[7] |= 0x20;

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
  if ((data[1] == 0x45) || (data[1] == 0x46)) {
    rapidCount++;
    data[3] = Rapid;
  } else {
    rapidCount = 0;
    data[3] = (state == Paused) ? Playing : state;
  }

  // disc
  data[3] |= disc % 6 + 1;

  // full magazine
  data[2] = 0b00111111;

  // changed, ready, reply
  static uint8_t last[4] = {0x02, 0x01, 0x00, 0x00};
  data[0] = 0x20;
  
  if (data[1] || memcmp(last, (data + 3), sizeof(last))) {
    if (data[1]) data[0] |= 0x40;
    data[0] |= 0x80;
    memcpy(last, (data + 3), sizeof(last));
  }

  data[1] = 0x00;
}


// get text for display
char *Player::getText(int8_t &haveText) {
  haveText = display.text[0] ? display.tag : 0;
  display.tag = abs(display.tag);
  return display.text;
}


// prepare text for Saab display
void Player::updateText() {
  uint8_t i, j;
  String text;

  if (state == Playing) {
    text = currentTrack.getTag(abs(display.tag) - 1);
  }

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    for (i = 0, j = 0; i < 12; i++, j++) {
      display.text[i] = text[j];
    }
    if (text[j] == 0x20) j++;
    for (; i < 23; i++, j++) {
      display.text[i] = text[j];
    }
    display.tag = -abs(display.tag);
  }
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

  Serial.println(currentTrack.name());
}


// find the new track number on the file system
void Player::openNextTrack() {
  File entry;
  uint16_t file_count;
  uint16_t dir_count;

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
  dir_count = path[depth].folder;
  if (trackNext > trackNum) {
    // start with the current file
    file_count = trackNum + 1;

    // was this the last file in the directory?
    if (file_count == path[depth].max) {
      path[depth].dir.rewindDirectory();
      dir_count++;
    }
  } else {
    // start at the top of the directory
    file_count = path[depth].min;
    path[depth].dir.rewindDirectory();
  }

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

      // if we've discovered the new track
      if (file_count > trackNext) { 
        // rollback the file count
        file_count = path[depth].min;
      } else {
        // count this folder if it contained files
        if (path[depth].min != path[depth].max) dir_count++;
      }
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
