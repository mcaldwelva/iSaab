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
  flac = false;
  buffer = NULL;
}


void AudioFile::operator=(const File &file) {
  File::operator=(file);
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
  for (uint8_t i = 0, j = 0; i < ssize && j < dsize; i++) {
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
      read(tag, 4);

      // get size
      read(buffer, 4);
      tag_size = (ver > 3) ? LE7x4(buffer) : LE8x4(buffer);

      // skip flags
      read(buffer, 2);
    } else {
      // get id
      read(tag, 3);

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
          || !strncasecmp_P(tag, (Id3v20Fields + i * ID3V20_ID), ID3V20_ID)) {
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

  // get number of other comments
  read(buffer, 4);
  tag_count = BE8x4(buffer);

  // search through comments
  for (int i = 0; i < tag_count; i++) {
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


void AudioFile::readFlacHeader() {
  uint8_t block_type;
  bool last_block;
  uint32_t block_size;

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
        // process comment block
        readVorbisComments();
        break;

      default:
        seek(position() + block_size);
        break;
    }
  } while (!last_block);
}


void AudioFile::readOggHeader() {
  uint8_t seg_count;
  uint16_t seg_size;

  // skip header info
  seek(position() + 22);

  // size of segment table
  seg_count = read();

  // read segment table
  seg_size = 0;
  read(buffer, seg_count);
  for (int i = 0; i < seg_count; i++) {
    seg_size += (uint8_t) buffer[i];
  }
  seek(position() + seg_size);

  // verify beginning of page
  read(buffer, 4);
  if (!strncmp_P((char *)buffer, PSTR("OggS"), 4)) {
    // skip header info
    seek(position() + 22);

    // skip segment table
    seg_count = read();
    seek(position() + seg_count + 7);

    // process comment block
    readVorbisComments();
  }

  // rewind
  seek(0);
}


// read important tags, return minimal header
int AudioFile::readHeader(uint8_t *&buf) {
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
    // MP3/AAC with ID3v2.x tags
    readId3Header(header[3]);
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


// makes a block-aligned read from the current position
// returns a pointer to the buffer and the number of bytes read
int AudioFile::readBlock(uint8_t *&buf) {
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
void AudioFile::close() {
  flac = false;
  buffer = NULL;

  for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
    tags[i] = "";
  }

  File::close();
}

