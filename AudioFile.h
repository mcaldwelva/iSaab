/*
 * AudioFile - extends the SDLib::File class to provide some nice-to-have
 *             features when working with the VS1053b sound coprocessor
 *
 * 02/01/2016 Mike C. - v 1.0x
 */

#ifndef AUDIOFILE_H
#define AUDIOFILE_H
#include <SD.h>

#define TAG_BUFFER 64
#define MAX_TAG_LENGTH 24

class AudioFile : public File
{
  public:
    enum Tag : uint8_t { Title, Album, Band, Artist, Genre, Year, MAX_TAG_ID };

    AudioFile();
    void operator=(const File &file);
    int readHeader(uint8_t *&buf);
    int readBlock(uint8_t *&buf);
    bool isFlac() { return flac; };
    String getTag(uint8_t tag);

  private:
    bool flac;
    uint8_t *buffer;
    String tags[MAX_TAG_ID];

    void updateTag(uint8_t tag, char src[], uint8_t ssize);
    void readId3Header(uint8_t ver);
    void readVorbisComments();
    int readFlacHeader();
    void readOggHeader();
    void readWmaHeader();
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
  "TPE1"
  "TPE2"
  "TCON"
  "TYER";

#define ID3V20_ID 3
const char Id3v20Fields[] PROGMEM = 
  "TT2"
  "TAL"
  "TP1"
  "TP2"
  "TCO"
  "TYE";

// WMA
#define GUID 16
const char ASF_Header_Object[] PROGMEM =
  {"\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C"};
const char ASF_Content_Description_Object[] PROGMEM =
  {"\x33\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C"};
const char ASF_Extended_Content_Description_Object[] PROGMEM =
  {"\x40\xA4\xD0\xD2\x07\xE3\xD2\x11\x97\xF0\x00\xA0\xC9\x5E\xA8\x50"};

#define ASF_ID 15
const char AsfFields[] PROGMEM =
  "Title\x0         "
  "WM/AlbumTitle\x0 "
  "WM/AlbumArtist\x0"
  "Author\x0        "
  "WM/Genre\x0      "
  "WM/Year\x0       ";
#endif // AUDIOFILE_H

