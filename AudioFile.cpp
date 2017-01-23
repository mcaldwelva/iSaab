/*
 * AudioFile - extends the SDLib::File class to provide some nice-to-have
 *             features when working with the VS1053b sound coprocessor
 *
 * 02/01/2016 Mike C. - v 1.0x
 */

#include "AudioFile.h"

// various macros to interpret multi-byte integers
#define LE7x4(x) (((uint32_t)((uint8_t)x[0])) << 21 | ((uint32_t)((uint8_t)x[1])) << 14 | ((uint32_t)((uint8_t)x[2])) << 7 | ((uint32_t)((uint8_t)x[3])))
#define LE8x4(x) (((uint32_t)((uint8_t)x[0])) << 24 | ((uint32_t)((uint8_t)x[1])) << 16 | ((uint32_t)((uint8_t)x[2])) << 8 | ((uint32_t)((uint8_t)x[3])))
#define LE8x3(x) (((uint32_t)((uint8_t)x[0])) << 16 | ((uint32_t)((uint8_t)x[1])) <<  8 | ((uint32_t)((uint8_t)x[2])))
#define BE8x4(x) (((uint32_t)((uint8_t)x[3])) << 24 | ((uint32_t)((uint8_t)x[2])) << 16 | ((uint32_t)((uint8_t)x[1])) << 8 | ((uint32_t)((uint8_t)x[0])))


AudioFile::AudioFile() {
  buffer = SdVolume::cacheClear();
}


void AudioFile::operator=(const File &file) {
  File::operator=(file);

  // reset properties
  flac = false;
  for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
    tags[i] = "";
  }
}


String AudioFile::getTag(uint8_t tag) {
  if (tag < MAX_TAG_ID) {
    return tags[tag];
  } else {
    String empty;
    return empty;
  }
}


// copy ascii/wide/unicode string to ascii
void AudioFile::asciiStringCopy(String &dst, char src[], uint8_t dsize, uint8_t ssize) {
  for (uint8_t i = 0; i < ssize && dst.length() < dsize; i++) {
    if (32 <= src[i] && src[i] <= 126) {
      dst += src[i];
    }
  }
}


void AudioFile::readId3Header(uint8_t ver) {
  char buffer[TAG_BUFFER];
  uint32_t header_end;
  char tag[4];
  uint32_t tag_size;

  // skip minor version & extended header info
  read(buffer, 2);

  // get header size
  read(buffer, 4);
  header_end = position() + LE7x4(buffer);

  // search through tags
  do {
    if (ver >= 3) {
      // get id
      read(tag, ID3V23_ID);

      // get size
      read(buffer, 4);
      tag_size = (ver > 3) ? LE7x4(buffer) : LE8x4(buffer);

      // skip flags
      read(buffer, 2);
    } else {
      // get id
      read(tag, ID3V20_ID);

      // get size
      read(buffer, 3);
      tag_size = LE8x3(buffer);
    }

    // read the tag field
    int32_t skip = tag_size - TAG_BUFFER;
    if (skip < 0) {
      read(buffer, tag_size);
    } else {
      tag_size = TAG_BUFFER;
      read(buffer, tag_size);
      seek(position() + skip);
    }

    // store it if it's one we care about
    for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
      if ((ver >= 3 && !strncasecmp_P(tag, (Id3v23Fields + i * ID3V23_ID), ID3V23_ID))
          || (ver < 3 && !strncasecmp_P(tag, (Id3v20Fields + i * ID3V20_ID), ID3V20_ID))) {
        asciiStringCopy(tags[i], buffer, MAX_TAG_LENGTH, tag_size);
        break;
      }
    }
  } while (tag_size > 0 && position() < header_end);

  // skip to the end
  seek(header_end);
}


void AudioFile::readVorbisComments() {
  char buffer[TAG_BUFFER];
  uint32_t tag_count;
  uint32_t tag_size;

  // skip vendor comments
  read(buffer, 4);
  tag_size = BE8x4(buffer);
  seek(position() + tag_size);

  // get number of tags
  read(buffer, 4);
  tag_count = BE8x4(buffer);

  // search through tags
  while (tag_count-- > 0) {
    // read field size
    read(buffer, 4);
    tag_size = BE8x4(buffer);

    // read the tag field
    int32_t skip = tag_size - TAG_BUFFER;
    if (skip < 0) {
      read(buffer, tag_size);
    } else {
      tag_size = TAG_BUFFER;
      read(buffer, tag_size);
      seek(position() + skip);
    }

    // store it if it's one we care about
    uint8_t delim = strchr(buffer, '=') - buffer + 1;
    for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
      if (!strncasecmp_P(buffer, (VorbisFields + i * VORBIS_ID), delim)) {
        asciiStringCopy(tags[i], (buffer + delim), MAX_TAG_LENGTH, tag_size - delim);
        break;
      }
    }
  }
}


int AudioFile::readFlacHeader() {
  char buffer[4];
  uint8_t block_type;
  bool last_block;
  uint32_t block_size;

  // read metablocks
  do {
    // block header
    if (read(buffer, 4) == -1) break;
    block_type = buffer[0] & 0x7F;
    last_block = buffer[0] & 0x80;
    block_size = LE8x3((buffer + 1));

    // block data
    switch (block_type) {
      case 0: // streaminfo
        seek(position() + block_size);
        return position();

      case 4: // vorbis_comment
        // process comment block
        readVorbisComments();
        break;

      default:
        seek(position() + block_size);
        break;
    }
  } while (!last_block);

  return 0;
}


void AudioFile::readOggHeader() {
  int seg_count;
  uint16_t seg_size;

  // skip header info
  seek(position() + 22);

  // size of segment table
  seg_count = read();

  // read segment table
  seg_size = 0;
  while (seg_count-- > 0) {
    seg_size += read();
  }

  // skip to the next block
  seek(position() + seg_size + 26);

  // skip segment table
  seg_count = read();
  seek(position() + seg_count + 7);

  // process comment block
  readVorbisComments();

  // rewind
  seek(0);
}


// reads audio header information, returning the minimum 
// returns a pointer to the buffer and the number of bytes read
int AudioFile::readHeader(uint8_t *&buf) {
  int siz = 0;
  buf = buffer;

  if (position() == 0) {
    // beginning of header
    char id[4];
    read(id, 4);

    if (!strncmp_P(id, PSTR("fLaC"), 4)) {
      // FLAC with Vorbis comments
      siz = readFlacHeader();
      flac = true;
    }
    else if (!strncmp_P(id, PSTR("ID3"), 3)) {
      // MP3/AAC with ID3v2.x tags
      readId3Header(id[3]);
    }
    else if (!strncmp_P(id, PSTR("OggS"), 4)) {
      // Ogg with Vorbis comments
      readOggHeader();
    } else {
      seek(0);
    }
  }  else {
    // continuing header
    if (isFlac()) {
        readFlacHeader();
    }
  }

  // end of header
  if (siz == 0) {
    // use file name if no title found
    if (tags[Title].length() == 0) {
      tags[Title] = name();
    }
  }

  return siz;
}


// makes a block-aligned read from the current position
// returns a pointer to the buffer and the number of bytes read
int AudioFile::readBlock(uint8_t *&buf) {
  uint32_t pos = position();
  uint16_t rem = pos % 512;
  uint16_t siz = 512 - rem;

  // ensure the block we need is in cache
  read();
  buf = buffer + rem;

  if (!seek(pos + siz)) {
    siz = size() - pos;
    seek(pos + siz);
  }

  return siz;
}

