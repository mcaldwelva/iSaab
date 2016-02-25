/*
 * tags -- A minimal tag parser for FLAC and MP3
 *
 * 07/04/2015 Mike C. - v 0.1
 */

#ifndef MEDIAFILE_H
#define MEDIAFILE_H
#include <SD.h>

#define TAG_BUFFER 64

class MediaFile : public File
{
  public:
    enum Tag : uint8_t { Title, Album, Artist, Year };

    MediaFile();
    void operator=(const File &file);
    int readHeader(uint8_t *&buf);
    int readBlock(uint8_t *&buf);
    bool isFlac();
    String getTag(uint8_t tag);
    void close();

  private:
    bool flac;
    uint8_t *buffer;
    String tags[4];

    void asciiStringCopy(String &dst, char src[], uint8_t dsize, uint8_t ssize);
    void readTag(char tag[], uint32_t size);
    void readTag(uint32_t size);
    void readFlacHeader();
    void readMp3Header(uint8_t ver);
};

#endif // MEDIAFILE_H

