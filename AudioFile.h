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
    enum Tag : uint8_t { Title, Album, Band, Artist, Genre, Year };

    AudioFile();
    void operator=(const File &file);
    int readHeader(uint8_t *&buf);
    int readBlock(uint8_t *&buf);
    bool isFlac();
    String getTag(uint8_t tag);
    void close();

  private:
    bool flac;
    uint8_t *buffer;
    String tags[Year + 1];

    void asciiStringCopy(String &dst, char src[], uint8_t dsize, uint8_t ssize);
    void readTag(char tag[], uint32_t size);
    void readTag(uint32_t size);
    void readId3Header(uint8_t ver);
    void readFlacHeader();
    void readOggHeader();
};

#endif // AUDIOFILE_H

