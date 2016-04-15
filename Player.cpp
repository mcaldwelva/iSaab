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
// and next track selected in a few seconds
#define RANDOM_TRACK (random(path[depth].tracks + 509) + path[depth].base)

Player::Player() {
  // initial player state
  state = Off;
  rapidCount = 0;
  display.tag = 0;

  // select the first track
  current = 0;
  next = 0;

  // no music files in the root
  depth = -1;
  path[0].folder = 0;
  path[0].base = 0;
  path[0].tracks = 0;
  path[0].hasFolders = true;
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
#if (DEBUGMODE>=1)
  Serial.println(F("BEGIN"));
#endif
  if (state == Off) {
    // turn on sound card
    if (!VS1053::begin()) {
#if (DEBUGMODE>=1)
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
    path[++depth].h = SD.open(F("/"));

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
#if (DEBUGMODE>=1)
  Serial.println(F("END"));
#endif
  if (state != Off) {
    // resume current track on start-up
    if (next == UNKNOWN) {
      next = current;
    }

    // close any open file
    stopTrack();

    // collapse path structure
    while (depth >= 0) {
      path[depth--].h.close();
    }

    // turn off sound card
    VS1053::end();

    state = Off;
  }
}


// main playback loop
void Player::play() {
  while (state != Off) {
#if (DEBUGMODE>=1)
    Serial.println(F("PLAY: nothing playing"));
#endif
    // get the next track if one hasn't already been selected
    if (next == UNKNOWN) {
      nextTrack();
    }
    openNextTrack();

    startTrack();
    updateText();

    playTrack();
    updateText();
  }
}


void Player::standby() {
#if (DEBUGMODE>=1)
  Serial.println(F("STANDBY"));
#endif
  if (state != Off) {
    state = Standby;
    updateText();
  }
}


void Player::pause() {
#if (DEBUGMODE>=1)
  Serial.println(F("PAUSE"));
#endif
  if (state == Playing) {
    state = Paused;
    updateText();
  }
}


void Player::resume() {
#if (DEBUGMODE>=1)
  Serial.println(F("RESUME"));
#endif
  if (state != Off) {
    state = Playing;
    updateText();
  }
}


void Player::shuffle() {
  shuffled = !shuffled;
#if (DEBUGMODE>=1)
  Serial.print(F("SHUFFLE "));
  Serial.println(shuffled ? F("ON") : F("OFF"));
#endif
}


void Player::nextTrack() {
#if (DEBUGMODE>=1)
  Serial.println(F("NEXT"));
#endif

  if (shuffled) {
    do { next = RANDOM_TRACK; } while (next == current);
  } else {
    // if no track is queued up
    if (next == UNKNOWN) {
      next = current + 1;
    } else {
      // keep skipping
      next++;
    }
  }

  stopTrack();
}


void Player::prevTrack() {
#if (DEBUGMODE>=1)
  Serial.println(F("PREVIOUS"));
#endif

  if (trackTime()) {
    // start this track over again
    next = current;
  } else {
    if (shuffled) {
      do { next = RANDOM_TRACK; } while (next == current);
    } else {
      // if no track is queued up
      if (next == UNKNOWN) {
        next = current - 1;
      } else {
        // keep skipping
        next--;
      }
    }
  }

  stopTrack();
}


void Player::nextDisc() {
#if (DEBUGMODE>=1)
  Serial.println(F("NEXT DISC"));
#endif

  if (shuffled) {
    display.tag = (abs(display.tag) + 1) % (AudioFile::MAX_TAG_ID + 1);
    updateText();
  } else {
    next = (path[depth].base + path[depth].tracks);
    stopTrack();
  }
}


// read presets from file
void Player::readPresets(const __FlashStringHelper* fileName) {
  // clear existing presets
  memset(presets, 0, sizeof(current) * NUM_PRESETS);

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
#if (DEBUGMODE>=1)
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
    next = presets[memory - 1];
    stopTrack();
  }
}


void Player::rewind() {
#if (DEBUGMODE>=1)
  Serial.println(F("REWIND"));
#endif
  rapidCount++;
  state = Rapid;
  updateText();

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
#if (DEBUGMODE>=1)
  Serial.println(F("FAST FORWARD"));
#endif
  rapidCount++;
  state = Rapid;
  updateText();

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


void Player::normal() {
#if (DEBUGMODE>=2)
  Serial.println(F("NORMAL"));
#endif
  if (state == Rapid) {
    state = Playing;
    updateText();
    rapidCount = 0;
  }
}


// gather player status for Saab
void Player::getStatus(uint8_t data[]) {
  uint16_t time;
  uint8_t track;
  uint8_t disc;

  // gather track information
  if (next == UNKNOWN) {
    // playing current track
    time = trackTime();
    track = current - path[depth].base + 1;
    disc = path[depth].folder;
  }
  else if (next >= (path[depth].base + path[depth].tracks)) {
    // new track is on the next disc
    time = 0;
    track = next - (path[depth].base + path[depth].tracks) + 1;
    disc = path[depth].folder + 1;
  }
  else if (next < path[depth].base) {
    // new track is on the previous disc
    time = 0;
    track = 100 - (current - next);
    disc = path[depth].folder - 1;
  }
  else {
    // new track is on the current disc
    time = 0;
    track = next - path[depth].base + 1;
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
  data[3] = (state == Paused) ? Playing : state;

  // disc
  data[3] |= disc % 9 + 1;

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
int Player::getText(char *&buf) {
  int ret = display.text[0] ? display.tag : 0;
  display.tag = abs(display.tag);
  buf = display.text;
  return ret;
}


// prepare text for Saab display
void Player::updateText() {
  uint8_t i, j;
  String text;

  if (state == Playing) {
    text = audio.getTag(abs(display.tag) - 1);
  }

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
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
    Serial.print(path[i].h.name());
    Serial.print(F(", index:"));
    Serial.print(path[i].base);
    Serial.print(F(", files:"));
    Serial.print(path[i].tracks);
    Serial.println();
  }

  Serial.println(audio.name());
}


// find the new track number on the file system
void Player::openNextTrack() {
  File entry;
  uint16_t file;
  uint16_t folder;

#if (DEBUGMODE>=1)
  Serial.print(F("OPENNEXTTRACK: "));
  Serial.println(next, DEC);
#endif

  // collapse current path as necessary
  while (path[depth].base > next) {
    path[depth--].h.close();
  }

top:
  folder = path[depth].folder;

  // determine where to start in the current directory
  if (next > current) {
    // start with the current file
    file = current + 1;

    // was this the last file in the directory?
    if (file == (path[depth].base + path[depth].tracks)) {
      // count this folder if it contained files
      folder++;

      // rewind if it contained folders we can explore
      if (path[depth].hasFolders && depth < MAX_DEPTH - 1) path[depth].h.rewindDirectory();
    }
  } else {
    // start at the top of the directory
    file = path[depth].base;
    path[depth].h.rewindDirectory();
  }

#if (DEBUGMODE>=1)
  Serial.print(F("Starting at:"));
  Serial.println(file, DEC);
#endif

  // search from here until we find our file
  while (state != Off) {
    // explore this directory if we haven't already or we've already 
    // explored this directory and we know the file is in here
    entry = path[depth].h.openNextFile();
    if (file < (path[depth].base + path[depth].tracks)) {
      while (entry) {
        if (!entry.isDirectory()) {
#if (DEBUGMODE>=2)
          Serial.print(file);
          Serial.print(' ');
          Serial.println(entry.name());
#endif

          if (file++ == next && path[depth].tracks != MAX_FILES) {
            // this is the file we're looking for
            ATOMIC_BLOCK(ATOMIC_FORCEON) {
              audio = entry;
              current = next;
              next = UNKNOWN;
            }

#if (DEBUGMODE>=1)
            dumpPath();
#endif
            return;
          } else {
            // skip this file
            entry.close();
          }
        } else {
          // skip this directory
          entry.close();
          path[depth].hasFolders = true;
        }
        entry = path[depth].h.openNextFile();
      }

      // we know how many files are in this dir now
      path[depth].tracks = file - path[depth].base;

      // if we've discovered the new track
      if (file > next) { 
        // start from the top of the directory
        file = path[depth].base;
        path[depth].h.rewindDirectory();
      } else {
        // count this folder if it contained files
        if (path[depth].tracks) folder++;

        // rewind if it contained folders we can explore
        if (path[depth].hasFolders && depth < MAX_DEPTH - 1) path[depth].h.rewindDirectory();
      }
    } else {

      // start checking sub-directories
      while (entry && !entry.isDirectory()) {
        entry.close();
        entry = path[depth].h.openNextFile();
      }

      // if we find a sub-dir, go in if we can
      if (entry && depth < MAX_DEPTH - 1) {
#if (DEBUGMODE>=1)
        Serial.print(folder);
        Serial.print(' ');
        Serial.println(entry.name());
#endif

        // getStatus needs these to be consistent
        ATOMIC_BLOCK(ATOMIC_FORCEON) {
          depth++;
          path[depth].folder = folder;
          path[depth].base = file;
        }
        path[depth].h = entry;
        path[depth].tracks = MAX_FILES;
        path[depth].hasFolders = false;
      } else {
        // if there are no sub-dirs
        if (depth > 0) {
          // pop out
          path[depth--].h.close();
        } else {
          // go back to the beginning
          next %= file;
#if (DEBUGMODE>=1)
          Serial.print(folder, DEC);
          Serial.println(F(" folders"));
          Serial.print(file, DEC);
          Serial.println(F(" files"));
#endif
          goto top;
        }
      }
    }
  }
}
