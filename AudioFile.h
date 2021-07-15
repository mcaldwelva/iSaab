#ifndef AUDIOFILE_H
#define AUDIOFILE_H
#include <SD.h>

#define MAX_TAG_LENGTH 24

// various macros to interpret multi-byte integers
#define BE7x4(x) (((uint32_t)((uint8_t)x[0])) << 21 | ((uint32_t)((uint8_t)x[1])) << 14 | ((uint32_t)((uint8_t)x[2])) << 7 | ((uint32_t)((uint8_t)x[3])))
#define BE8x4(x) (((uint32_t)((uint8_t)x[0])) << 24 | ((uint32_t)((uint8_t)x[1])) << 16 | ((uint32_t)((uint8_t)x[2])) << 8 | ((uint32_t)((uint8_t)x[3])))
#define BE8x3(x) (((uint32_t)((uint8_t)x[0])) << 16 | ((uint32_t)((uint8_t)x[1])) <<  8 | ((uint32_t)((uint8_t)x[2])))
#define LE8x4(x) (((uint32_t)((uint8_t)x[3])) << 24 | ((uint32_t)((uint8_t)x[2])) << 16 | ((uint32_t)((uint8_t)x[1])) << 8 | ((uint32_t)((uint8_t)x[0])))
#define LE8x2(x) (((uint16_t)((uint8_t)x[1])) << 8 | ((uint16_t)((uint8_t)x[0])))

class AudioFile : public File
{
  public:
    enum Tag : uint8_t { Title, Album, Band, Artist, Genre, Year, NUM_TAGS };

    AudioFile();
    AudioFile& operator=(const File &file);
    void close();

    int readMetadata(uint8_t *&buf);
    int readBlock(uint8_t *&buf);
    bool jump(int16_t secs, uint16_t rate);
    bool isHighBitRate() { return type == FLAC || type == DSF; }
    const String &getTag(Tag tag) {
      return tags[ tag < NUM_TAGS ? tag : NUM_TAGS ];
    }
    uint8_t *fillBuffer(uint8_t c, size_t n) {
      return (uint8_t *) memset(buffer, c, n);
    }

  private:
    enum Type : uint8_t { FLAC, DSF, OTHER } type;
    uint8_t *buffer;
    String tags[NUM_TAGS + 1];

    void readTag(uint8_t tag, uint16_t ssize);
    void readId3Tags();
    void readVorbisComments();
    int readFlac();
    void readOgg();
    void readQtff();
    void readAsf();
    void readDsf();
};

#define VORBIS_ID 12
const char VorbisFields[] PROGMEM = 
  "TITLE=      "
  "ALBUM=      "
  "ALBUMARTIST="
  "ARTIST=     "
  "GENRE=      "
  "DATE=       ";

#define ID3V23_ID 4
const char Id3v23Fields[] PROGMEM = 
  "TIT2"
  "TALB"
  "TPE2"
  "TPE1"
  "TCON"
  "TYER";

#define ID3V20_ID 3
const char Id3v20Fields[] PROGMEM = 
  "TT2"
  "TAL"
  "TP2"
  "TP1"
  "TCO"
  "TYE";

#define QTFF_ID 4
const char iTunesFields[] PROGMEM =
  "\xA9""nam"
  "\xA9""alb"
  "\x61""ART"
  "\xA9""ART"
  "\xA9""gen"
  "\xA9""day";

const char iTunesPath[] PROGMEM =
  "moov""udta""meta""ilst";

#define ASF_ID 15
const char AsfFields[] PROGMEM =
  "Title\x0         "
  "WM/AlbumTitle\x0 "
  "WM/AlbumArtist\x0"
  "Author\x0        "
  "WM/Genre\x0      "
  "WM/Year\x0       ";

#define GUID 16
const byte ASF_Header_Object[] PROGMEM =
  {0x30,0x26,0xB2,0x75,0x8E,0x66,0xCF,0x11,0xA6,0xD9,0x00,0xAA,0x00,0x62,0xCE,0x6C};
const byte ASF_Content_Description_Object[] PROGMEM =
  {0x33,0x26,0xB2,0x75,0x8E,0x66,0xCF,0x11,0xA6,0xD9,0x00,0xAA,0x00,0x62,0xCE,0x6C};
const byte ASF_Extended_Content_Description_Object[] PROGMEM =
  {0x40,0xA4,0xD0,0xD2,0x07,0xE3,0xD2,0x11,0x97,0xF0,0x00,0xA0,0xC9,0x5E,0xA8,0x50};

#endif // AUDIOFILE_H
