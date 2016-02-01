/*
 * tags -- A minimal tag parser for FLAC and MP3
 *
 * 07/04/2015 Mike C. - v 0.1
 */

#ifndef MEDIAFILE_H
#define MEDIAFILE_H
#include <SD.h>

#define TAG_BUFFER 32

class MediaFile : public File
{
  public:
    char title[24];
    char artist[24];
    char album[24];
    bool highBitRate;

    void operator= (const SDLib::File &file);
    int readHeader(void *buf, uint16_t nbyte);

  private:
    void asciiStringCopy(char dst[], char src[], uint8_t dsize, uint8_t ssize);
    void readTag(char tag[], uint32_t size);
    void readTag(uint32_t size);
    void readFlacHeader();
    void readMp3Header(uint8_t ver);
};

#endif // MEDIAFILE_H

