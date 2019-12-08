/*
 *  Player extends VS1053 audio file playback to an entire file system
 *   - Folders are searched in depth-first order
 *   - Files are played in file system order
 *
 */

#include <SD.h>
#include <util/atomic.h>
#include "Player.h"


// perform one-time setup on power-up
void Player::setup() {
  // deselect SD card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // setup VS1053 pins
  VS1053::setup();

  // initialize shuffler
  uint16_t t;
  for (uint8_t i = 0; i < 16; i++) {
    t <<= 1;
    t |= analogRead(0) & 1;
  }
  seed = t ? t : 1;

  // initialize display
  tag = AudioFile::NUM_TAGS;
}


// start-up player
void Player::begin() {
  // turn on sound card
  VS1053::begin();

  // open SD card
  if (SD.begin(25000000, SD_CS)
      && (path[0].h = SD.open("/"))) {
    current = UNKNOWN;

    // load FLAC patch
    loadPlugin(F("PATCH053.BIN"));

    // read presets
    readPresets(F("PRESETS.TXT"));

    // promote ready state
    if (state == Busy) {
      state = Paused;
    }
  }
}


// shut-down player
void Player::end() {
  // resume current track on start-up
  next = current;

  // close SD card
  while (depth > 0) {
    path[depth--].h.close();
  }
  path[0].h.close();
  SD.end();

  // turn off sound card
  VS1053::end();

  state = Off;
}


// main playback loop
void Player::play() {
  if (state >= Busy) {
    begin();

    while (state >= Paused) {
      // get the next track if one hasn't already been selected
      if (next == UNKNOWN) {
        nextTrack();
      }
      openNextTrack();
      startTrack();
      playTrack();
    }

    end();
  }
}


void Player::on() {
  if (state == Off) {
    state = Busy;
  }
}


void Player::off() {
  if (state >= Paused) {
    state = Busy;
    stopTrack();
  }
}


void Player::pause() {
  state = Paused;
}


void Player::resume() {
  state = Playing;
}


void Player::shuffle() {
  repeatCount++;

  if (repeatCount == 1) {
    shuffled = !shuffled;
  }
}


void Player::nextTrack() {
  repeatCount++;

  if (shuffled) {
    if (next == UNKNOWN) {
      do { next = xorshift(path[depth].first, path[depth].last + 500); } while (next == current);
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
  repeatCount++;

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
  repeatCount++;

  if (repeatCount == 1 && next == UNKNOWN) {
    next = path[depth].last;
    stopTrack();
  }
}


// read presets from file
void Player::readPresets(const __FlashStringHelper* fileName) {
  // clear existing presets
  memset(presets, 0, sizeof(presets));

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
  repeatCount++;

  if (repeatCount == 1 && next == UNKNOWN) {
    next = presets[memory];
    stopTrack();
  }
}


void Player::rewind() {
  repeatCount++;

  if (state != Rapid) {
    state = Rapid;
  }

  skip(-repeatCount);
}


void Player::forward() {
  repeatCount++;

  if (state != Rapid) {
    state = Rapid;
  }

  skip(repeatCount);
}


void Player::normal() {
  repeatCount = 0;

  if (state == Rapid) {
    state = Playing;
  }
}


void Player::nextText() {
  repeatCount++;

  if (repeatCount == 1) {
    tag = (tag + 1) % (AudioFile::NUM_TAGS + 1);
    updated = true;
  }
}


void Player::text(uint8_t id) {
  repeatCount++;

  if (repeatCount == 1) {
      if (id == tag) {
        tag = AudioFile::NUM_TAGS;
      } else {
        tag = id;
        updated = true;
      }
  }
}


bool Player::getText(String &text) {
  bool ret = updated;

  if (audio && state == Playing && tag < AudioFile::NUM_TAGS) {
    updated = false;
    text = audio.getTag(tag);
  } else {
    updated = true;
    text = "";
  }

  return ret;
}


// find the new track number on the file system
void Player::openNextTrack() {
  static bool hasFolders;

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
  while (state >= Paused) {

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
        if (entry.isDirectory()) {
          // flag hasFolders
          hasFolders = true;
        } else {
          // count file
          if (file == next && path[depth].last != UNKNOWN) {
            // this is the file we're looking for
            ATOMIC_BLOCK(ATOMIC_FORCEON) {
              current = file;
              next = UNKNOWN;
              audio = entry;
            }
            return;
          } else {
            file++;
          }
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
        depth++;
        path[depth].folder = folder;
        path[depth].first = file;
        path[depth].h = entry;
        path[depth].last = UNKNOWN;
        hasFolders = false;
      } else {
        // there are no sub-dirs
        if (depth > 0) {
          // pop out
          path[depth--].h.close();
        } else {
          // end of file system
          next %= file;
          folder = 0;
        }
      }
    }
  }
}


uint16_t Player::xorshift(uint16_t min, uint16_t max) {
  seed ^= seed << 7;
  seed ^= seed >> 9;
  seed ^= seed << 8;

  return seed % (max - min) + min;
}
