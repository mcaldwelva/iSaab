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


MediaFile::MediaFile() {
  flac = false;
  buffer = NULL;
}


void MediaFile::operator=(const File &file) {
  File::operator=(file);
}

String MediaFile::getTag(uint8_t tag) {
  if (tag <= Year) {
    return tags[tag];
  } else {
    String empty;
    return empty;
  }
}

// copy ascii/wide/unicode string to ascii
void MediaFile::asciiStringCopy(String &dst, char src[], uint8_t dsize, uint8_t ssize) {
  for (uint8_t i = 0, j = 0; i < ssize && j < dsize; i++) {
    if (32 <= src[i] && src[i] <= 126) {
      dst += src[i];
    }
  }
}


// read an ID3 tag and store it if it's one we care about
inline __attribute__((always_inline))
void MediaFile::readTag(char tag[], uint32_t size) {
  char buffer[TAG_BUFFER];
  int32_t skip;

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
    asciiStringCopy(tags[Title], buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TALB"), 4) || !strncmp_P(tag, PSTR("TAL"), 3)) {
    asciiStringCopy(tags[Album], buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TPE1"), 4) || !strncmp_P(tag, PSTR("TP1"), 3)) {
    asciiStringCopy(tags[Artist], buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TPE2"), 4) || !strncmp_P(tag, PSTR("TP2"), 3)) {
    asciiStringCopy(tags[Band], buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TCON"), 4) || !strncmp_P(tag, PSTR("TCO"), 3)) {
    asciiStringCopy(tags[Genre], buffer, 24, size);
  }
  else if (!strncmp_P(tag, PSTR("TYER"), 4) || !strncmp_P(tag, PSTR("TYE"), 3)) {
    asciiStringCopy(tags[Year], buffer, 4, size);
  }
//  else if (!strncmp_P(tag, PSTR("XRVA"), 4) || !strncmp_P(tag, PSTR("RVA2"), 3)) {
//  }
}


// read a Vorbis comment and store it if it's one we care about
inline __attribute__((always_inline))
void MediaFile::readTag(uint32_t size) {
  char buffer[TAG_BUFFER];
  int32_t skip;

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
    asciiStringCopy(tags[Title], (buffer + 6), 24, size - 6);
  }
  else if (!strncasecmp_P(buffer, PSTR("ALBUM="), 6)) {
    asciiStringCopy(tags[Album], (buffer + 6), 24, size - 6);
  }
  else if (!strncasecmp_P(buffer, PSTR("ARTIST="), 7)) {
    asciiStringCopy(tags[Artist], (buffer + 7), 24, size - 7);
  }
  else if (!strncasecmp_P(buffer, PSTR("ALBUMARTIST="), 12)) {
    asciiStringCopy(tags[Band], (buffer + 12), 24, size - 12);
  }
  else if (!strncasecmp_P(buffer, PSTR("GENRE="), 6)) {
    asciiStringCopy(tags[Genre], (buffer + 6), 24, size - 6);
  }
  else if (!strncasecmp_P(buffer, PSTR("DATE="), 5)) {
    asciiStringCopy(tags[Year], (buffer + 5), 4, size - 5);
  }
//  else if (!strncasecmp_P(buffer, PSTR("REPLAYGAIN_TRACK_GAIN="), 22)) {
//  }
}


void MediaFile::readMp3Header(uint8_t ver) {
  char tag[4];
  uint32_t header_end;
  uint32_t tag_size = 1;

  // skip minor version & extended header info
  read(buffer, 2);

  // get header size
  read(buffer, 4);
  header_end = position() + LE7x4(buffer);

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
      read(buffer, 2);
    } else {
      // v2.0 - v2.2
      read(tag, 3);

      // read tag size
      read(buffer, 3);
      tag_size = LE8x3(buffer);
    }

    readTag(tag, tag_size);
  }

  // skip to the end
  seek(header_end);
}


void MediaFile::readFlacHeader() {
  uint8_t block_type;
  bool last_block;
  uint32_t block_size;
  uint32_t tag_count;
  uint32_t tag_size;

  // keep "fLaC"
  buffer+= 4;
  
  // read metablocks
  do {
    // block header
    read(buffer, 4);
    block_type = buffer[0] & 0x7F;
    last_block = buffer[0] & 0x80;
    block_size = LE8x3((buffer + 1));

    // block data
    switch (block_type) {
      case 0: // streaminfo
        // make this the last block
        buffer[0] |= 0x80;
        buffer+= 4;
        read(buffer, block_size);
        buffer+= block_size;
        break;

      case 4: // vorbis_comment
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
        break;

      default:
        seek(position() + block_size);
        break;
    }
  } while (!last_block);
}


void MediaFile::readOggHeader() {
  uint8_t seg_count;
  uint16_t seg_size;
  uint32_t tag_count;
  uint32_t tag_size;

  seek(position() + 22);
  seg_count = read();
  read(buffer, seg_count);

  seg_size = 0;
  for (int i = 0; i < seg_count; i++) {
    seg_size += (uint8_t) buffer[i];
  }
  seek(position() + seg_size);

  read(buffer, 4);
  if (!strncmp_P((char *)buffer, PSTR("OggS"), 4)) {
    seek(position() + 22);
    seg_count = read();
    seek(position() + seg_count + 7);

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
  }

  // rewind for now
  seek(0);
}


// read important tags, return minimal header
int MediaFile::readHeader(uint8_t *&buf) {
  char header[48];
  buffer = (uint8_t *)&header;
  seek(0);

  // determine file type
  read(header, 4);
  if (!strncmp_P(header, PSTR("fLaC"), 4)) {
    // FLAC with Vorbis comments
    readFlacHeader();
    flac = true;
  }
  else if (!strncmp_P(header, PSTR("ID3"), 3)) {
    // MP3 with ID3v2.x tags
    readMp3Header(header[3]);
  }
  else if (!strncmp_P(header, PSTR("OggS"), 4)) {
    // Ogg with Vorbis comments
    readOggHeader();
  } else {
    seek(0);
  }

  // use file name if no title found
  if (tags[Title].length() == 0) {
    tags[Title] = name();
  }

  // copy header to cache
  uint16_t siz = buffer - (uint8_t *)&header;
  buffer = SdVolume::cacheClear();
  memcpy(buffer, header, siz);

  // return cache pointer
  buf = buffer;
  return siz;
}


bool MediaFile::isFlac() {
  return flac;
}


// makes a block-aligned read from the current position
// returns a pointer to the buffer and the number of bytes read
int MediaFile::readBlock(uint8_t *&buf) {
  uint32_t pos = position();
  uint16_t rem = pos % 512;
  uint16_t siz = flac ? 512 - rem : 32 - (rem % 32);

  // ensure the block we need is in cache
  read();
  buf = buffer + rem;

  if (!seek(pos + siz)) {
    siz = size() - pos;
    seek(pos + siz);
  }

  return siz;
}


// reset properties on closing
void MediaFile::close() {
  flac = false;
  buffer = NULL;

  for (uint8_t i = 0; i <= Year; i++) {
    tags[i] = "";
  }

  File::close();
}

