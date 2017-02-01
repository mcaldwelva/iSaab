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
#define BE8x2(x) (((uint16_t)((uint8_t)x[1])) << 8 | ((uint16_t)((uint8_t)x[0])))


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


// append ascii chars from string to specified tag
void AudioFile::updateTag(uint8_t tag, char src[], uint8_t ssize) {
  for (uint8_t i = 0; i < ssize && tags[tag].length() < MAX_TAG_LENGTH; i++) {
    if (' ' <= src[i] && src[i] <= '~') {
      tags[tag] += src[i];
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
    uint32_t skip = position() + tag_size;
    if (tag_size > TAG_BUFFER) {
      tag_size = TAG_BUFFER;
    }

    // store it if it's one we care about
    for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
      if ((ver >= 3 && !strncasecmp_P(tag, (Id3v23Fields + i * ID3V23_ID), ID3V23_ID))
          || (ver < 3 && !strncasecmp_P(tag, (Id3v20Fields + i * ID3V20_ID), ID3V20_ID))) {
        read(buffer, tag_size);
        updateTag(i, buffer, tag_size);
        break;
      }
    }

    seek(skip);
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
    uint32_t skip = position() + tag_size;
    if (tag_size > TAG_BUFFER) {
      tag_size = TAG_BUFFER;
    }
    read(buffer, tag_size);

    // store it if it's one we care about
    uint8_t delim = strchr(buffer, '=') - buffer + 1;
    for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
      if (!strncasecmp_P(buffer, (VorbisFields + i * VORBIS_ID), delim)) {
        updateTag(i, (buffer + delim), tag_size - delim);
        break;
      }
    }

    seek(skip);
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


void AudioFile::readWmaHeader() {
  char buffer[TAG_BUFFER];

  // Object ID
  seek(GUID);

  // Object Size
  read(buffer, 8);

  // Number of Header Objects
  read(buffer, 4);
  uint32_t object_count = BE8x4(buffer);

  // Reserved Bytes
  read(buffer, 2);

  // for each object
  while (object_count-- > 0) {
    uint32_t next_object;

    read(buffer, GUID);
    if (!memcmp_P(buffer, ASF_Content_Description_Object, GUID)) {
      // Object Size
      read(buffer, 4);
      next_object = position() - 20 + BE8x4(buffer);
      read(buffer, 4);

      // read field lengths
      read(buffer, 2);
      uint32_t title_size = BE8x2(buffer);
      read(buffer, 2);
      uint32_t artist_size = BE8x2(buffer);
      read(buffer, 6);

      // read the tag field
      uint32_t skip = position() + title_size;
      if (title_size > TAG_BUFFER) {
        title_size = TAG_BUFFER;
      }
      read(buffer, title_size);
      seek(skip);
      updateTag(Title, buffer, title_size);

      // read the tag field
      skip = position() + artist_size;
      if (artist_size > TAG_BUFFER) {
        artist_size = TAG_BUFFER;
      }
      read(buffer, artist_size);
      seek(skip);
      updateTag(Artist, buffer, artist_size);
    }
    else if (!memcmp_P(buffer, ASF_Extended_Content_Description_Object, GUID)) {
      // Object Size
      read(buffer, 4);
      next_object = position() - 20 + BE8x4(buffer);
      read(buffer, 4);

      // Content Descriptors Count
      read(buffer, 2);
      uint16_t tag_count = BE8x2(buffer);

      while (tag_count-- > 0) {
        // Descriptor Name Length
        read(buffer, 2);
        uint16_t name_size = BE8x2(buffer);

        // Descriptor Name
        uint32_t skip = position() + name_size;
        name_size /= 2;
        if (name_size > ASF_ID) {
          name_size = ASF_ID;
        }
        for (uint8_t j = 0; j < name_size; j++) {
          read((buffer + j), 2);
        }
        seek(skip);

        // Descriptor Value Data Type
        read();
        read();

        // Descriptor Value Length
        read((buffer + name_size), 2);
        uint16_t value_size = BE8x2((buffer + name_size));
        skip = position() + value_size;
        if (value_size > TAG_BUFFER) {
          value_size = TAG_BUFFER;
        }

        // store it if it's one we care about
        for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
          if (!strncmp_P(buffer, (AsfFields + i * ASF_ID), name_size)) {
            // Descriptor Value
            read(buffer, value_size);
            updateTag(i, buffer, value_size);
            break;
          }
        }

        // next descriptor
        seek(skip);
      }
    }
    else {
      // Object Size
      read(buffer, 4);
      next_object = position() - 20 + BE8x4(buffer);
    }

    // next object
    seek(next_object);
  }

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
    }
    else if (!memcmp_P((const char *) buffer, ASF_Header_Object, GUID)) {
      // WMA file
      readWmaHeader();
    }
    else {
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

