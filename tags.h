/*
 * tags -- A minimal tag parser for FLAC and MP3
 *
 * 07/04/2015 Mike C. - v 0.1
 */

#ifndef TAGS_H
#define TAGS_H
#include <SD.h>

class tags
{
  public:
    static void getTags(File file);

  private:
    static void asciiStringCopy(char dst[], char src[], uint8_t dsize, uint8_t ssize);
    static void readTag(File file, char tag[], unsigned long size);
    static void readTag(File file, unsigned long size);
    static void readFlacTags(File file);
    static void readMp3Tags(File file, unsigned short ver);
};

#endif // TAGS_H

