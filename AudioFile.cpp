/*
 * AudioFile - extends the SDLib::File class to provide some nice-to-have
 *             features when working with the VS1053b sound coprocessor
 *
 * 02/01/2016 Mike C. - v 1.0x
 */

#include "AudioFile.h"

// various macros to interpret multi-byte integers
#define BE7x4(x) (((uint32_t)((uint8_t)x[0])) << 21 | ((uint32_t)((uint8_t)x[1])) << 14 | ((uint32_t)((uint8_t)x[2])) << 7 | ((uint32_t)((uint8_t)x[3])))
#define BE8x4(x) (((uint32_t)((uint8_t)x[0])) << 24 | ((uint32_t)((uint8_t)x[1])) << 16 | ((uint32_t)((uint8_t)x[2])) << 8 | ((uint32_t)((uint8_t)x[3])))
#define BE8x3(x) (((uint32_t)((uint8_t)x[0])) << 16 | ((uint32_t)((uint8_t)x[1])) <<  8 | ((uint32_t)((uint8_t)x[2])))
#define LE8x4(x) (((uint32_t)((uint8_t)x[3])) << 24 | ((uint32_t)((uint8_t)x[2])) << 16 | ((uint32_t)((uint8_t)x[1])) << 8 | ((uint32_t)((uint8_t)x[0])))
#define LE8x2(x) (((uint16_t)((uint8_t)x[1])) << 8 | ((uint16_t)((uint8_t)x[0])))


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


// read ascii tag value directly from buffer
void AudioFile::readTag(uint8_t tag, uint16_t ssize) {
  uint16_t j = position() % 512;
  read();

  for (uint16_t i = 0; i < ssize && tags[tag].length() < MAX_TAG_LENGTH; i++) {
    char c = buffer[j++];
    if (' ' <= c && c <= '~') {
      tags[tag] += c;
    }

    // advance buffer if needed
    if (j == 512) {
      seek(position() + i % 512);
      read();
      j = 0;
    }
  }
}


void AudioFile::readId3Header() {
  char buffer[4];
  uint32_t header_end;
  char tag[ID3V23_ID];
  uint32_t tag_size;

  // major version
  seek(3);
  uint8_t ver = read();

  // minor version and flags
  seek(position() + 2);

  // header size
  read(buffer, 4);
  header_end = position() + BE7x4(buffer);

  // search through tags
  do {
    if (ver >= 3) {
      // get id
      read(tag, ID3V23_ID);

      // get size
      read(buffer, 4);
      tag_size = (ver > 3) ? BE7x4(buffer) : BE8x4(buffer);

      // skip flags
      seek(position() + 2);
    } else {
      // get id
      read(tag, ID3V20_ID);

      // get size
      read(buffer, 3);
      tag_size = BE8x3(buffer);
    }

    // locate next tag
    uint32_t skip = position() + tag_size;

    // store it if it's one we care about
    for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
      if (ver >= 3 ? !strncasecmp_P(tag, (Id3v23Fields + i * ID3V23_ID), ID3V23_ID)
                   : !strncasecmp_P(tag, (Id3v20Fields + i * ID3V20_ID), ID3V20_ID)) {
        readTag(i, tag_size);
        break;
      }
    }

    // next tag
    seek(skip);
  } while (tag_size > 0 && position() < header_end);

  // skip to the end
  seek(header_end);
}


void AudioFile::readVorbisComments() {
  char buffer[VORBIS_ID];
  uint32_t tag_count;
  uint32_t tag_size;

  // vendor comments
  read(buffer, 4);
  tag_size = LE8x4(buffer);
  seek(position() + tag_size);

  // number of tags
  read(buffer, 4);
  tag_count = LE8x4(buffer);

  // search through tags
  while (tag_count-- > 0 && this) {
    // read field size
    read(buffer, 4);
    tag_size = LE8x4(buffer);

    // locate next tag
    uint32_t skip = position() + tag_size;

    // read tag name
    read(buffer, VORBIS_ID);
    uint8_t delim = (uint16_t) memchr(buffer, '=', VORBIS_ID) - (uint16_t) &buffer + 1;

    // store it if it's one we care about
    for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
      if (!strncasecmp_P(buffer, (VorbisFields + i * VORBIS_ID), delim)) {
        seek(position() - (VORBIS_ID - delim));
        readTag(i, tag_size - delim);
        break;
      }
    }

    // next tag
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
    read(buffer, 4);
    block_type = buffer[0] & 0x7F;
    last_block = buffer[0] & 0x80;
    block_size = BE8x3((buffer + 1));

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
  } while (!last_block && this);

  return 0;
}


void AudioFile::readOggHeader() {
  int seg_count;
  uint16_t seg_size;

  // skip header info
  seek(26);

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


void AudioFile::readAsfHeader() {
  char buffer[GUID];

  // Object ID & Size
  seek(GUID + 8);

  // Number of Header Objects
  read(buffer, 4);
  uint32_t object_count = LE8x4(buffer);

  // Reserved Bytes
  seek(position() + 2);

  // for each object
  while (object_count-- > 0 && this) {
    uint32_t next_object;

    read(buffer, GUID);
    if (!memcmp_P(buffer, ASF_Content_Description_Object, GUID)) {
      // Object Size
      read(buffer, 4);
      next_object = position() - 20 + LE8x4(buffer);
      seek(position() + 4);

      // Title Length
      read(buffer, 2);
      uint16_t title_size = LE8x2(buffer);

      // Author Length
      read(buffer, 2);
      uint16_t artist_size = LE8x2(buffer);

      // Copyright + Description + Rating Length
      read(buffer, 6);

      // Title
      uint32_t skip = position() + title_size;
      readTag(Title, title_size);
      seek(skip);

      // Author
      readTag(Artist, artist_size);
    }
    else if (!memcmp_P(buffer, ASF_Extended_Content_Description_Object, GUID)) {
      // Object Size
      read(buffer, 4);
      next_object = position() - 20 + LE8x4(buffer);
      seek(position() + 4);

      // Content Descriptors Count
      read(buffer, 2);
      uint16_t tag_count = LE8x2(buffer);

      while (tag_count-- > 0) {
        // Descriptor Name Length
        read(buffer, 2);
        uint16_t name_size = LE8x2(buffer);

        // Descriptor Name & Value Data Type
        uint32_t skip = position() + name_size + 2;
        name_size /= 2;
        if (name_size > ASF_ID) {
          name_size = ASF_ID;
        }
        for (uint8_t j = 0; j < name_size; j++) {
          read((buffer + j), 2);
        }
        seek(skip);

        // Descriptor Value Length
        char w[2];
        read(w, 2);
        uint16_t value_size = LE8x2(w);

        // Descriptor Value
        skip = position() + value_size;

        // store it if it's one we care about
        for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
          if (!strncmp_P(buffer, (AsfFields + i * ASF_ID), name_size)) {
            readTag(i, value_size);
            break;
          }
        }

        // next Descriptor
        seek(skip);
      }
    }
    else {
      // Object Size
      read(buffer, 4);
      next_object = position() - 20 + LE8x4(buffer);
    }

    // next Object
    seek(next_object);
  }

  // rewind
  seek(0);
}


void AudioFile::readQtffHeader() {
  char buffer[QTFF_ID];
  uint32_t next_atom;
  uint32_t parent_atom = size();
  uint8_t depth = 0;

  // skip 'ftyp' atom
  seek(0x20);

  do {
    // atom size
    read(buffer, 4);
    next_atom = position() - 4 + BE8x4(buffer);

    // atom name
    read(buffer, 4);

    // if we're not in tag list
    if (depth < 4) {
      // determine if this atom is in the path to tags
      if (!memcmp_P(buffer, (iTunesPath + depth * QTFF_ID), QTFF_ID)) {
        if (depth++ == 2) {
          // skip 'meta' version info
          read(buffer, 4);
        }
        parent_atom = next_atom;
      } else {
        // skip to next atom
        seek(next_atom);
      }
    } else {
      // read tag value
      for (uint8_t i = 0; i < MAX_TAG_ID; i++) {
        if (!memcmp_P(buffer, (iTunesFields + i * QTFF_ID), QTFF_ID)) {
          // skip to 'data' value
          seek(position() + 16);
          uint16_t value_size = next_atom - position();
          readTag(i, value_size);
          break;
        }
      }

      // next tag
      seek(next_atom);
    }

  } while (position() < parent_atom);

  // rewind
  seek(0);
}


// reads audio header information
// returns a pointer to the buffer and the number of bytes read
int AudioFile::readHeader(uint8_t *&buf) {
  int siz = 0;
  buf = buffer;

  if (position() == 0) {
    // beginning of header
    read();

    // look for supported magic numbers
    switch(BE8x4(buffer)) {
      case 0x664c6143:
        seek(4);
        siz = readFlacHeader();
        flac = true;
        break;
      case 0x4f676753:
        readOggHeader();
        break;
      case 0x3026b275:
        readAsfHeader();
        break;
      case 0x00000020:
        readQtffHeader();
        break;
      default:
        if (BE8x3(buffer) == 0x494433) {
          readId3Header();
        } else {
          seek(0);
        }
    }
  }  else {
    // continuing header
    if (flac) {
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

