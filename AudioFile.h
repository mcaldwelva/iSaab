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
    void close();

  private:
    bool flac;
    uint8_t *buffer;
    String tags[MAX_TAG_ID];

    void asciiStringCopy(String &dst, char src[], uint8_t dsize, uint8_t ssize);
    void readId3Header(uint8_t ver);
    void readVorbisComments();
    void readFlacHeader();
    void readOggHeader();
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

#endif // AUDIOFILE_H

