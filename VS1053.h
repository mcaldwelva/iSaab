/*
 *  VS1053 - Provides a simplified interface for playing files
 *           through a VLSI VS1053b sound coprocessor
 *
 *  07/04/2015 Mike C. - v1.0
 */

#ifndef VS1053_H
#define VS1053_H

#include <SD.h>

#define VS1053_LARGE_BUFFER  512
#define VS1053_SMALL_BUFFER  32

#define VS1053_XRESET        9    // VS1053 Reset pin (output)
#define VS1053_XCS           7    // VS1053 SPI Control select pin (output)
#define VS1053_XDCS          6    // VS1053 SPI Data select pin (output)
#define VS1053_XDREQ         3    // VS1053 Data Request (input)
#define VS1053_IRQ           digitalPinToInterrupt(VS1053_XDREQ)

#define VS_WRITE_COMMAND     0x02
#define VS_READ_COMMAND      0x03

#define SCI_MODE             0x00
#define SCI_STATUS           0x01
#define SCI_BASS             0x02
#define SCI_CLOCKF           0x03
#define SCI_DECODETIME       0x04
#define SCI_AUDATA           0x05
#define SCI_WRAM             0x06
#define SCI_WRAMADDR         0x07
#define SCI_HDAT0            0x08
#define SCI_HDAT1            0x09
#define SCI_VOLUME           0x0B

#define SS_DO_NOT_JUMP       0x8000

#define SM_DIFF              0x0001
#define SM_LAYER12           0x0002
#define SM_RESET             0x0004
#define SM_CANCEL            0x0008
#define SM_EARSPKLO          0x0010
#define SM_TESTS             0x0020
#define SM_STREAM            0x0040
#define SM_SDINEW            0x0800
#define SM_ADPCM             0x1000
#define SM_LINE1             0x4000
#define SM_CLKRANGE          0x8000

#define SCI_AIADDR           0x0a
#define SCI_AICTRL0          0x0c
#define SCI_AICTRL1          0x0d
#define SCI_AICTRL2          0x0e
#define SCI_AICTRL3          0x0f

#define XP_BYTERATE          0x1e05
#define XP_ENDFILLBYTE       0x1e06

// active codec in HDAT1
#define CODEC_UNKNOWN        0x0000
#define CODEC_FLAC           0x664c
#define CODEC_WAV            0x7665
#define CODEC_WMA            0x574d
#define CODEC_MIDI           0x4d54
#define CODEC_OGG            0x4f67
#define CODEC_AAC_ADTS       0x4154
#define CODEC_AAC_ADIF       0x4144
#define CODEC_AAC_MP4        0x4d34
#define CODEC_AAC_LATM       0x4c41
#define CODEC_MP3_ID3V2      0x4944
#define CODEC_MP3_MIN        0xffe0
#define CODEC_MP3_MAX        0xffff

class VS1053 {
  public:
    void setup();
    bool begin();
    void end();

    bool startTrack();
    void playTrack();
    void stopTrack();

    uint16_t trackTime();
    void skip(int);

  protected:
    void setVolume(uint8_t, uint8_t);
    bool loadPlugin(const __FlashStringHelper*);

    enum State : uint8_t { Off = 0, Standby = 3, Playing = 4, Paused = 5, Rapid = 6 };
    volatile State state;
    File currentTrack;

  private:
    bool readyForData();
    void sendData(uint8_t[], uint16_t);
    bool largeBuffer;

    uint16_t sciRead(uint8_t);
    void sciWrite(uint8_t, uint16_t);
    void spiwrite(uint8_t);
    uint8_t spiread();

    int skippedTime;
};

// use the top of the heap for temporary buffer
extern int __heap_start, *__brkval;
#define buffer ((uint8_t *)(!__brkval ? &__heap_start : __brkval))

#endif // VS1053_H
