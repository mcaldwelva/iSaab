/*
 *  Player - Implements the essential features of a media player
 *           returns player status as a Saab-ready message
 *
 *  07/04/2015 Mike C. - v1.0
 */

#include <SD.h>
#include <util/atomic.h>
#include "Player.h"


Player::Player() {
  // initial player state
  repeatCount = 0;
  display.tag = 0;

  // select the first track
  next = 0;

  // no music files in the root
  depth = 0;
  path[0].folder = 0;
  path[0].first = 0;
  path[0].last = 0;
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
    seed |= analogRead(0) & 1;
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
    SD.begin(SD_CS);

    // open SD root
    path[0].h = SD.open("/");
    current = UNKNOWN;

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
    while (depth > 0) {
      path[depth--].h.close();
    }
    path[0].h.close();

    // turn off sound card
    VS1053::end();
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
  if (repeatCount == 1) {
    shuffled = !shuffled;
  }

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
    if (next == UNKNOWN) {
      do { next = random(path[depth].first, path[depth].last + 500); } while (next == current);
    }
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

  if (shuffled || repeatCount == 1 && trackTime()) {
    // start this track over again
    next = current;
  } else {
    // if no track is queued up
    if (next == UNKNOWN) {
      next = current - 1;
    } else {
      // keep skipping
      next--;
    }
  }

  if (next == UNKNOWN) {
    next = 0;
  }

  stopTrack();
}


void Player::nextDisc() {
#if (DEBUGMODE>=1)
  Serial.println(F("NEXT DISC"));
#endif

  if (repeatCount == 1) {
    if (shuffled) {
      display.tag = ((display.tag & 0x7f) + 1) % (AudioFile::MAX_TAG_ID + 1);
      updateText();
    } else {
      if (next == UNKNOWN) {
        next = path[depth].last;
        stopTrack();
      }
    }
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

  if (repeatCount == 1) {
    if (shuffled) {
      if (memory == (display.tag & 0x7f)) {
        display.tag = 0;
      } else {
        display.tag = memory;
      }
      updateText();
    } else {
      if (next == UNKNOWN) {
        next = presets[memory - 1];
        stopTrack();
      }
    }
  }
}


void Player::rewind() {
#if (DEBUGMODE>=1)
  Serial.println(F("REWIND"));
#endif
  state = Rapid;
  updateText();

  int8_t seconds;
  if (repeatCount >= 10) {
    seconds = -12;
  } else if (repeatCount >= 5) {
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
  state = Rapid;
  updateText();

  int8_t seconds;
  if (repeatCount >= 10) {
    seconds = +10;
  } else if (repeatCount >= 5) {
    seconds = +5;
  } else {
    seconds = +1;
  }
  skip(seconds);
}


void Player::normal() {
  if (state == Rapid) {
#if (DEBUGMODE>=2)
    Serial.println(F("NORMAL"));
#endif
    state = Playing;
    updateText();
  }

  repeatCount = 0;
}


// gather player status for Saab
void Player::getStatus(uint8_t data[]) {
  uint16_t time;
  uint8_t track;
  uint8_t disc;

  // keep track of repeated commands
  repeatCount++;

  // gather track information
  if (next == UNKNOWN) {
    // playing current track
    time = trackTime();
    track = current - path[depth].first + 1;
    disc = path[depth].folder;
  }
  else if (next >= path[depth].last) {
    // new track is on the next disc
    time = 0;
    track = next - path[depth].last + 1;
    disc = path[depth].folder + 1;
  }
  else if (next < path[depth].first) {
    // new track is on the previous disc
    time = 0;
    track = 100 - (current - next);
    disc = path[depth].folder - 1;
  }
  else {
    // new track is on the current disc
    time = 0;
    track = next - path[depth].first + 1;
    disc = path[depth].folder;
  }

  // married
  data[7] = 0xd0;

  // random
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
  data[3] = state & 0xf0;

  // disc
  data[3] |= disc % 9 + 1;

  // full magazine
  data[2] = 0b00111111;

  // ready
  data[0] = 0x20;

  // reply, changed
  static uint8_t last[4] = {0x02, 0x01, 0x00, 0x00};
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
  display.tag &= 0x7f;
  buf = display.text;
  return ret;
}


// prepare text for Saab display
void Player::updateText() {
  uint8_t i, j;
  String text;

  if (audio && state == Playing) {
    text = audio.getTag((display.tag & 0x7f) - 1);
  }

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    for (i = 0, j = 0; i < 12; i++, j++) {
      display.text[i] = text[j];
    }
    if (text[j] == 0x20) j++;
    for (; i < 23; i++, j++) {
      display.text[i] = text[j];
    }
    display.tag |= 0x80;
  }
}


void Player::dumpPath() {
  Serial.println(F("dumpPath..."));

  for (int i = 0; i <= depth; i++) {
    Serial.print(i, DEC);
    Serial.print(":");
    Serial.print(path[i].h.name());
    Serial.print(F(", index:"));
    Serial.print(path[i].first);
    Serial.print(F(", files:"));
    Serial.print(path[i].last - path[i].first);
    Serial.println();
  }

  Serial.println(audio.name());
}


// find the new track number on the file system
void Player::openNextTrack() {
  static bool hasFolders;

#if (DEBUGMODE>=1)
  Serial.print(F("OPENNEXTTRACK: "));
  Serial.println(next, DEC);
#endif

  // go back to the closest starting point
  while (next < path[depth].first) {
    path[depth--].h.close();
    hasFolders = true;
  }
  uint16_t folder = path[depth].folder;

  // skip this folder if possible
  uint16_t file;
  if (next >= path[depth].last) {
    file = path[depth].last;
    if (path[depth].last - path[depth].first > 0) folder++;
  } else {
    file = current + 1;
  }

  // search forward until we find the file
  File entry;
  while (state != Off) {

    // start from the top of the folder if necessary
    if (next < file) {
      file = path[depth].first;
      path[depth].h.rewindDirectory();
    }

    // explore this folder if the file may be here
    if (file < path[depth].last) {

      // enumerate files in this folder
      entry = path[depth].h.openNextFile();
      while (entry) {
        if (!entry.isDirectory()) {
#if (DEBUGMODE>=2)
          Serial.print(file);
          Serial.print(' ');
          Serial.println(entry.name());
#endif

          if (file++ == next && path[depth].last != UNKNOWN) {
            // this is the file we're looking for
            ATOMIC_BLOCK(ATOMIC_FORCEON) {
              current = next;
              next = UNKNOWN;
              audio = entry;
            }

#if (DEBUGMODE>=1)
            dumpPath();
#endif
            return;
          }
        } else {
          // flag hasFolders
          hasFolders = true;
        }

        entry.close();
        entry = path[depth].h.openNextFile();
      }

      // update with actual number of files
      path[depth].last = file;

      // count this folder if it contained files
      if (path[depth].last - path[depth].first > 0) folder++;

    } else {

      if (depth < MAX_DEPTH) {
        // rewind if there are explorable sub-folders
        if (file == path[depth].last && hasFolders) path[depth].h.rewindDirectory();

        // find the next folder
        entry = path[depth].h.openNextFile();
        while (entry && !entry.isDirectory()) {
          entry.close();
          entry = path[depth].h.openNextFile();
        }
      }

      // if we found one
      if (entry) {
#if (DEBUGMODE>=1)
        Serial.print(folder);
        Serial.print(' ');
        Serial.println(entry.name());
#endif

        // getStatus needs these to be consistent
        ATOMIC_BLOCK(ATOMIC_FORCEON) {
          depth++;
          path[depth].folder = folder;
          path[depth].first = file;
        }
        path[depth].h = entry;
        path[depth].last = UNKNOWN;
        hasFolders = false;
      } else {
        // there are no sub-dirs
        if (depth > 0) {
          // pop out
          path[depth--].h.close();
        } else {
#if (DEBUGMODE>=1)
          Serial.print(folder, DEC);
          Serial.println(F(" folders"));
          Serial.print(file, DEC);
          Serial.println(F(" files"));
#endif
          // end of file system
          next %= file;
          folder = 0;
        }
      }
    }
  }
}
