/*
 * tags -- A minimal tag parser for FLAC and MP3
 *
 * 07/04/2015 Mike C. - v 0.1
 */

#include "MediaFile.h"

// various macros to interpret multi-byte integers
#define LE7x4(x) (((uint32_t)((uint8_t)x[0])) << 21 | ((uint32_t)((uint8_t)x[1])) << 14 | ((uint32_t)((uint8_t)x[2])) << 7 | ((uint32_t)((uint8_t)x[3])))
#define LE8x4(x) (((uint32_t)((uint8_t)x[0])) << 24 | ((uint32_t)((uint8_t)x[1])) << 16 | ((uint32_t)((uint8_t)x[2])) << 8 | ((uint32_t)((uint8_t)x[3])))
#define LE8x3(x) (((uint32_t)((uint8_t)x[0])) << 16 | ((uint32_t)((uint8_t)x[1])) <<  8 | ((uint32_t)((uint8_t)x[2])))
#define BE8x4(x) (((uint32_t)((uint8_t)x[3])) << 24 | ((uint32_t)((uint8_t)x[2])) << 16 | ((uint32_t)((uint8_t)x[1])) << 8 | ((uint32_t)((uint8_t)x[0])))

char *buffer;

// effectively the constructor
void MediaFile::operator= (const SDLib::File &file) {
  File::operator=(file);

  highBitRate = false;
  memset(title, ' ', 24);
  memset(album, ' ', 24);
  memset(artist, ' ', 24);
}


void MediaFile::asciiStringCopy(char dst[], char src[], uint8_t dsize, uint8_t ssize) {
  for (int i = 0, j = 0; i < ssize && j < dsize; i++) {
    if (32 <= src[i] && src[i] <= 126) {
      dst[j++] = src[i];
    }
  }
}


// read an ID3 tag and store it if it's one we care about
inline __attribute__((always_inline))
void MediaFile::readTag(char tag[], uint32_t size) {
  long skip;

  // read the tag data
  skip = size - TAG_BUFFER;
  if (skip < 0) {
    read(buffer, size);
  } else {
    size = TAG_BUFFER;
    read(buffer, size);
    seek(position() + skip);
  }

  if (!strncmp_P(tag, PSTR("TIT2"), 4) || !strncmp_P(tag, PSTR("TT2"), 3)) {
    asciiStringCopy(title, buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TALB"), 4) || !strncmp_P(tag, PSTR("TAL"), 3)) {
    asciiStringCopy(album, buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TPE1"), 4) || !strncmp_P(tag, PSTR("TP1"), 3)) {
    asciiStringCopy(artist, buffer, 24, size);
  }
//  else if (!strncmp_P(tag, PSTR("XRVA"), 4) || !strncmp_P(tag, PSTR("RVA2"), 3)) {
//  }
}


// read a Vorbis comment and store it if it's one we care about
inline __attribute__((always_inline))
void MediaFile::readTag(uint32_t size) {
  long skip;

  // read the tag data
  skip = size - TAG_BUFFER;
  if (skip < 0) {
    read(buffer, size);
  } else {
    size = TAG_BUFFER;
    read(buffer, size);
    seek(position() + skip);
  }

  if (!strncasecmp_P(buffer, PSTR("TITLE="), 6)) {
    asciiStringCopy(title, (buffer + 6), 24, size - 6);
  }
  else if (!strncasecmp_P(buffer, PSTR("ALBUM="), 6)) {
    asciiStringCopy(album, (buffer + 6), 24, size - 6);
  }
  else if (!strncasecmp_P(buffer, PSTR("ARTIST="), 7)) {
    asciiStringCopy(artist, (buffer + 7), 24, size - 7);
  }
//  else if (!strncasecmp_P(buffer, PSTR("REPLAYGAIN_TRACK_GAIN="), 22)) {
//  }
}


void MediaFile::readMp3Header(uint8_t ver) {
  char tag[5];
  uint32_t header_end;
  uint32_t tag_size;

  // skip minor version & extended header info
  seek(position() + 2);

  // get header size
  tag_size = read(buffer, 4);
  header_end = position() + tag_size;

  // scan tags
  while (tag_size > 0 && position() < header_end) {
    if (ver >= 3) {
      // v2.3 or greater
      read(tag, 4);

      // read tag size based on version
      read(buffer, 4);
      if (ver > 3) {
        tag_size = LE7x4(buffer);
      } else {
        tag_size = LE8x4(buffer);
      }

      // skip flags
      seek(position() + 2);
    } else {
      // v2.0 - v2.2
      read(tag, 3);

      // read tag size
      read(buffer, 3);
      tag_size = LE8x3(buffer);
    }

    readTag(tag, tag_size);
  }

  // leave file sector aligned
  seek((header_end / 32) * 32);
}


void MediaFile::readFlacHeader() {
  uint8_t block_type;
  bool last_block;
  uint32_t block_size;
  uint32_t tag_count;
  uint32_t tag_size;

  // keep "fLaC"
  buffer+= 4;
  
  // search for comment block
  do {
    read(buffer, 4);
    block_type = buffer[0] & 0x7F;
    last_block = buffer[0] & 0x80;
    block_size = LE8x3((buffer + 1));

    // comment block
    if (block_type == 4) {
      // skip vendor comments
      read(buffer, 4);
      tag_size = BE8x4(buffer);
      seek(position() + tag_size);
  
      // get number of other comments
      read(buffer, 4);
      tag_count = BE8x4(buffer);
  
      // search through comments
      for (int i = 0; i < tag_count; i++) {
        // read tag size
        read(buffer, 4);
        tag_size = BE8x4(buffer);
  
        // read tag data
        readTag(tag_size);
      }
    } else {
      // stream info block
      if (block_type == 0) {
        // make this the last block
        buffer[0] |= 0x80;
        buffer+= 4;
        read(buffer, block_size);
        buffer+= block_size;
      } else {
        seek(position() + block_size);
      }
    }
  } while (!last_block);

  // leave file sector aligned
  seek((position() / 512) * 512);
}


// find header boundaries, read important tags,
// and let the caller know if this file requires a larger buffer
int MediaFile::readHeader(void *buf, uint16_t nbyte) {
  // setup tag buffer
  buffer = (char*) buf;
  seek(0);

  // determine file type
  read(buffer, 4);
  if (!strncmp_P(buffer, PSTR("fLaC"), 4)) {
    // FLAC with Vorbis comments
    readFlacHeader();
    highBitRate = true;
  }
  else if (!strncmp_P(buffer, PSTR("ID3"), 3)) {
    // MP3 with ID3v2.x tags
    readMp3Header(buffer[3]);
  }

  // use file name if no title found
  if (title[0] == ' ') {
    strcpy(title, name());
  }

  // use folder name if no album found
  if (album[0] == ' ') {
  }

  // use parent folder name if no artist found
  if (artist[0] == ' ') {
  }

  return buffer - (char*) buf;
}

