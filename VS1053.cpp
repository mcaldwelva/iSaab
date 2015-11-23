/*
 *  VS1053 - Provides a simplified interface for playing files
 *           through a VLSI VS1053b sound coprocessor
 *
 *  07/04/2015 Mike C. - v1.0
 */

#include <SPI.h>
#include <SD.h>
#include <util/atomic.h>
#include "VS1053.h"

#define VS1053_SCI_SETTING SPISettings(3000000, MSBFIRST, SPI_MODE0)
#define VS1053_SDI_SETTING SPISettings(12000000, MSBFIRST, SPI_MODE0)

// setup pins
void VS1053::setup() {
  // turn off sound card
  pinMode(VS1053_XRESET, OUTPUT);
  digitalWrite(VS1053_XRESET, LOW);

  // configure control pin
  pinMode(VS1053_XCS, OUTPUT);
  digitalWrite(VS1053_XCS, HIGH);

  // configure data pin
  pinMode(VS1053_XDCS, OUTPUT);
  digitalWrite(VS1053_XDCS, HIGH);

  // configure interrupt pin
  pinMode(VS1053_XDREQ, INPUT);

  SPI.begin();
}


// power on
bool VS1053::begin() {
  // turn on sound card
  digitalWrite(VS1053_XRESET, HIGH);
  delay(2);

  // turn down analog
  setVolume(0xfe, 0xfe);

  // set internal speed, SC_MULT=3.5x, SC_ADD=1.0x
  sciWrite(SCI_CLOCKF, 0x8800);
  delay(2);

  // simple check to see if the card is responding
  return sciRead(SCI_STATUS) & 0x40;
}


// power off
void VS1053::end() {
  // turn off sound card
  digitalWrite(VS1053_XRESET, LOW);
}


// cancel playback and close file
void VS1053::stopTrack() {
  // turn analog down
  setVolume(0xfe, 0xfe);

  // get codec specific fill byte
  sciWrite(SCI_WRAMADDR, XP_ENDFILLBYTE);
  uint8_t endFillByte = sciRead(SCI_WRAM);

  // cancel playback in case we had a bad file
  sciWrite(SCI_MODE, SM_SDINEW | SM_CANCEL);

  // send endFillByte until cancel is accepted
  while (sciRead(SCI_MODE) & SM_CANCEL) {
    ATOMIC_BLOCK(ATOMIC_FORCEON) {
      memset(buffer, endFillByte, VS1053_SMALL_BUFFER);
      sendData(buffer, VS1053_SMALL_BUFFER);
    }
  }

  // done
  currentTrack.close();
}


// assumes currentTrack is already open
bool VS1053::startTrack() {
  if (!currentTrack) {
    return false;
  }

  // reset decode time
  skippedTime = 0;
  sciWrite(SCI_DECODETIME, 0x00);
  sciWrite(SCI_DECODETIME, 0x00);

  // turn analog up
  setVolume(0x00, 0x00);

  // burn through header
  largeBuffer = true;
  State save = state;
  state = Playing;
  playTrack();
  state = save;

  // lossless codecs need a larger buffer
  uint16_t codec = sciRead(SCI_HDAT1);
  largeBuffer = (codec == CODEC_FLAC) || (codec == CODEC_WAV);

  return true;
}


// transfer as much data from file as needed to fill the card's internal buffer
void VS1053::playTrack() {
  int bytesRead;
  size_t bufferSize = largeBuffer ? VS1053_LARGE_BUFFER : VS1053_SMALL_BUFFER;

  // stay here as long as we need to
  while (state == Playing && currentTrack && readyForData()) {
    // this is critical for FLAC
    ATOMIC_BLOCK(ATOMIC_FORCEON) {
      bytesRead = currentTrack.read(buffer, bufferSize);
      sendData(buffer, bytesRead);
    }

    // close the file if there's no more data to read
    if (bytesRead < bufferSize) {
      currentTrack.close();
    }
  }
}


// skip the specified number of seconds
void VS1053::skip(int secs) {
  // check if the card will let us jump now
  if (sciRead(SCI_STATUS) & SS_DO_NOT_JUMP) {
    return;
  }

  // get average byterate
  sciWrite(SCI_WRAMADDR, XP_BYTERATE);
  long rate = sciRead(SCI_WRAM);

  // adjust rate based on codec
  if (largeBuffer) {
    rate <<= 2;
  } else {
    rate &= ~3;
  }

  // update position
  long pos = currentTrack.position() + rate * secs;
  if (currentTrack.seek(pos)) {
    skippedTime += secs;
  }
}


// get estimated track position in seconds
uint16_t VS1053::trackTime() {
  uint16_t ret = sciRead(SCI_DECODETIME);
  ret += skippedTime;
  return ret;
}


// load a patch or plugin from disk
bool VS1053::loadPlugin(const __FlashStringHelper* fileName) {
  uint8_t buff[2];
  uint16_t byteCount = 0, addr, count, val;
  uint32_t fileSize;

  File plugin = SD.open(fileName);
  if (plugin) {
    fileSize = plugin.size();
    while (byteCount < fileSize) {
      plugin.read(buff, 2);
      addr = buff[0];

      plugin.read(buff, 2);
      count = (buff[1] << 8) + buff[0];
      byteCount += 4;

      if (count & 0x8000)      // RLE run, replicate n samples
      {
        count &= 0x7FFF;
        plugin.read(buff, 2);
        val = (buff[1] << 8) + buff[0];
        while (count--) {
          sciWrite(addr, val);
        }

        byteCount += 2;
      }
      else                      // Copy run, copy n samples
      {
        while (count--) {
          plugin.read(buff, 2);
          val = (buff[1] << 8) + buff[0];
          sciWrite(addr, val);

          byteCount += 2;
        }
      }
    }

    plugin.close();
  }
}


// send data to the sound card
void VS1053::sendData(uint8_t data[], uint16_t len) {
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(VS1053_SDI_SETTING);
#endif
  fastDigitalWrite(VS1053_XDCS, LOW);

  for (int i = 0; i < len; i++) {
    // when DREQ is high, it's safe to send 32 bytes
    if (!(i & 31)) while (!readyForData());
    spiwrite(data[i]);
  }

  fastDigitalWrite(VS1053_XDCS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif
}


inline __attribute__((always_inline))
void VS1053::setVolume(uint8_t left, uint8_t right) {
  uint16_t v;
  v = left;
  v <<= 8;
  v |= right;

  sciWrite(SCI_VOLUME, v);
}


uint16_t VS1053::sciRead(uint8_t addr) {
  uint16_t data;

#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(VS1053_SCI_SETTING);
#endif
  fastDigitalWrite(VS1053_XCS, LOW);

  spiwrite(VS_READ_COMMAND);
  spiwrite(addr);
  delayMicroseconds(10);
  data = spiread();
  data <<= 8;
  data |= spiread();

  fastDigitalWrite(VS1053_XCS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif

  return data;
}


void VS1053::sciWrite(uint8_t addr, uint16_t data) {
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(VS1053_SCI_SETTING);
#endif
  fastDigitalWrite(VS1053_XCS, LOW);

  spiwrite(VS_WRITE_COMMAND);
  spiwrite(addr);
  spiwrite(data >> 8);
  spiwrite(data);

  fastDigitalWrite(VS1053_XCS, HIGH);
#ifdef SPI_HAS_TRANSACTION
  SPI.endTransaction();
#endif
}


// check if sound card can take data
inline __attribute__((always_inline))
bool VS1053::readyForData() {
  return fastDigitalRead(VS1053_XDREQ);
}

inline __attribute__((always_inline))
uint8_t VS1053::spiread()
{
  return SPI.transfer(0x00);
}

inline __attribute__((always_inline))
void VS1053::spiwrite(uint8_t c)
{
  SPI.transfer(c);
}
