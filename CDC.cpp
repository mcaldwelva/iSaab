/*
 *  CDC extends VS1053 audio file playback to an entire file system via CD changer style interface
 *   - Folders are searched in depth-first order
 *   - Files are played in file system order
 *
 */

#include <SD.h>
#include <util/atomic.h>
#include "CDC.h"

CDCClass CDC;

// perform one-time setup on power-up
void CDCClass::setup() {
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
}


// start-up CDC
void CDCClass::begin() {
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


// shut-down CDC
void CDCClass::end() {
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
void CDCClass::loop() {
  if (state >= Busy) {
    begin();

    while (state >= Paused) {
      // get the next track if one hasn't already been selected
      if (next == UNKNOWN) {
        skipTrack();
      }
      openTrack();
      playTrack();
    }

    end();
  }
}


void CDCClass::on() {
  if (state == Off) {
    state = Busy;
  }
}


void CDCClass::off() {
  if (state >= Paused) {
    state = Busy;
    stopTrack();
  }
}


void CDCClass::pause() {
  state = Paused;
}


void CDCClass::resume() {
  state = Playing;
}


void CDCClass::shuffle() {
  shuffled = !shuffled;
}


void CDCClass::skipTrack(int8_t sign) {
  if (shuffled) {
    if (sign > 0) {
      if (next == UNKNOWN) {
        do { next = xorshift(path[depth].first, path[depth].last + 500); } while (next == current);
      }
    } else {
      next = current;
    }
  } else {
    if (next == UNKNOWN) {
      next = current;
    }
    if (sign > 0) {
      next++;
    } else if (sign < 0) {
      next--;
    }
  }

  // if next is UNKNOWN at this point, it has wrapped
  if (next == UNKNOWN) {
    next = 0;
  }

  stopTrack();
}


void CDCClass::nextDisc() {
  if (next == UNKNOWN) {
    next = path[depth].last;
    stopTrack();
  }
}


// read presets from file
void CDCClass::readPresets(const __FlashStringHelper* fileName) {
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


void CDCClass::preset(uint8_t memory) {
  if (next == UNKNOWN) {
    next = presets[memory];
    stopTrack();
  }
}


void CDCClass::skipTime(int8_t seconds) {
  state = Rapid;
  skip(seconds);
}


void CDCClass::normal() {
  if (state == Rapid) {
    state = Playing;
  }
}


// find the new track number on the file system
void CDCClass::openTrack() {
  static bool hasFolders = true;

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
    if (hasFolders) path[depth].h.rewindDirectory();
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
          // only count audio files
          char *ext = strchr(entry.name(), '.');
          switch (LE8x4(ext)) {
            case LE8x4(".AAC"):
            case LE8x4(".DSF"):
            case LE8x4(".FLA"):
            case LE8x4(".M4A"):
            case LE8x4(".MP3"):
            case LE8x4(".OGG"):
            case LE8x4(".WMA"):
              if (file == next && path[depth].last != UNKNOWN) {
                // this is the file we're looking for
                ATOMIC_BLOCK(ATOMIC_FORCEON) {
                  current = file;
                  next = UNKNOWN;
                  audio = entry;
                }
                return;
              } else {
                // count file
                file++;
              }
          }
        }

        entry.close();
        entry = path[depth].h.openNextFile();
      }

      // we now know the last file in this folder
      path[depth].last = file;

      // count this folder if it contained files
      if (path[depth].last - path[depth].first > 0) folder++;

      // rewind if there are explorable sub-folders
      if (hasFolders) path[depth].h.rewindDirectory();

    } else {

      if (hasFolders && depth < MAX_DEPTH) {
        // find the next folder
        entry = path[depth].h.openNextFile();
        while (entry && !entry.isDirectory()) {
          entry.close();
          entry = path[depth].h.openNextFile();
        }
      }

      // if we found a folder
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
        hasFolders = true;
      }
    }
  }
}


uint16_t CDCClass::xorshift(uint16_t min, uint16_t max) {
  seed ^= seed << 7;
  seed ^= seed >> 9;
  seed ^= seed << 8;

  return seed % (max - min) + min;
}
