/*
 * tags -- A minimal tag parser for FLAC and MP3
 *
 * 07/04/2015 Mike C. - v 0.1
 */

#include "tags.h"

// various macros to interpret multi-byte integers
#define LE7x4(x) ((unsigned) x[0] << 21 | (unsigned) x[1] << 14 | (unsigned) x[2] << 7 | (unsigned) x[3])
#define LE8x4(x) ((unsigned) x[0] << 24 | (unsigned) x[1] << 16 | (unsigned) x[2] << 8 | (unsigned) x[3])
#define LE8x3(x) ((unsigned) x[0] << 16 | (unsigned) x[1] <<  8 | (unsigned) x[2])
#define BE8x4(x) ((unsigned) x[3] << 24 | (unsigned) x[2] << 16 | (unsigned) x[1] << 8 | (unsigned) x[0])

static char title[24];
static char artist[24];
static char album[24];

void tags::asciiStringCopy(char dst[], char src[], uint8_t dsize, uint8_t ssize) {
  for (int i = 0, j = 0; i < ssize && j < dsize; i++) {
    if (32 <= src[i] && src[i] <= 126) {
      dst[j++] = src[i];
    }
  }
}


inline __attribute__((always_inline))
void tags::readTag(File file, char tag[], unsigned long size) {
  char buffer[60];
  long skip;

  // read the tag data
  skip = size - sizeof(buffer);
  if (skip < 0) {
    file.read(buffer, size);
  } else {
    size = sizeof(buffer);
    file.read(buffer, size);
    file.seek(file.position() + skip);
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
  else if (!strncmp_P(tag, PSTR("XRVA"), 4) || !strncmp_P(tag, PSTR("RVA2"), 3)) {
  }
}


inline __attribute__((always_inline))
void tags::readTag(File file, unsigned long size) {
  char buffer[66];
  long skip;

  // read the tag data
  skip = size - sizeof(buffer);
  if (skip < 0) {
    file.read(buffer, size);
  } else {
    size = sizeof(buffer);
    file.read(buffer, size);
    file.seek(file.position() + skip);
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
  else if (!strncasecmp_P(buffer, PSTR("REPLAYGAIN_TRACK_GAIN="), 22)) {
  }
}


void tags::readMp3Tags(File file, unsigned short ver) {
  char buffer[4];
  char tag[5];
  unsigned long block_end;
  unsigned long tag_size;

  // skip minor version & extended header info
  file.seek(file.position() + 2);

  // get header size
  file.read(buffer, 4);
  block_end = file.position() + LE7x4(buffer);

  // scan tags
  while (file.position() < block_end) {
    if (ver >= 3) {
      // v2.3 or greater
      file.read(tag, 4);

      // read tag size based on version
      file.read(buffer, 4);
      if (ver > 3) {
        tag_size = LE7x4(buffer);
      } else {
        tag_size = LE8x4(buffer);
      }

      // skip flags
      file.seek(file.position() + 2);
    } else {
      // v2.0 - v2.2
      file.read(tag, 3);

      // read tag size
      file.read(buffer, 3);
      tag_size = LE8x3(buffer);
    }

    readTag(file, tag, tag_size);
  }
}


void tags::readFlacTags(File file) {
  char buffer[4];
  unsigned int block_type;
  bool last_block;
  unsigned long block_end;
  unsigned long tag_count;
  unsigned long tag_size;

  // search for comment block
  do {
    file.read(buffer, 4);
    block_type = buffer[0] & 0x7F;
    last_block = buffer[0] & 0x80;
    block_end = file.position() + LE8x3((buffer + 1));
    if (block_type != 4) {
      file.seek(block_end);
    }
  } while (!last_block && block_type != 4);

  // if we have the comment block
  if (block_type == 4) {
    // skip vendor comments
    file.read(buffer, 4);
    tag_size = BE8x4(buffer);
    file.seek(file.position() + tag_size);

    // get number of other comments
    file.read(buffer, 4);
    tag_count = BE8x4(buffer);

    // search through comments
    for (int i = 0; i < tag_count; i++) {
      // read tag size
      file.read(buffer, 4);
      tag_size = BE8x4(buffer);

      // read tag data
      readTag(file, tag_size);
    }
  }
}


// returns the track title in the specified buffer
void tags::getTags(File file) {
  char buffer[4];

  // clear storage
  memset(title, ' ', 24);
  memset(album, ' ', 24);
  memset(artist, ' ', 24);  
  file.seek(0);

  // determine file type
  file.read(buffer, 4);
  if (!strncmp_P(buffer, PSTR("ID3"), 3)) {
    // MP3 with ID3v2.x tags
    readMp3Tags(file, buffer[3]);
  }
  else if (!strncmp_P(buffer, PSTR("fLaC"), 4)) {
    // FLAC with Vorbis comments
    readFlacTags(file);
  }

  // use file name if no title found
  if (title[0] == ' ') {
    strcpy(title, file.name());
  }
  Serial.print(F("Title: "));
  for (int i = 0; i < 24; i++) {
    Serial.print(title[i]);
  }
  Serial.println();

  // use folder name if no album found
  if (album[0] == ' ') {
  }
  Serial.print(F("Album: "));
  for (int i = 0; i < 24; i++) {
    Serial.print(album[i]);
  }
  Serial.println();

  // use parent folder name if no artist found
  if (artist[0] == ' ') {
  }
  Serial.print(F("Artist: "));
  for (int i = 0; i < 24; i++) {
    Serial.print(artist[i]);
  }
  Serial.println();

  // be kind, rewind
  file.seek(0);
}

