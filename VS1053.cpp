/*
 *  VS1053 - Provides a simplified interface for playing files
 *           through a VLSI VS1053b sound coprocessor
 *
 *  07/04/2015 Mike C. - v1.0
 */

#include <SPI.h>
#include <SD.h>
#include "VS1053.h"

#define VS1053_SCI_SETTING SPISettings(1750000, MSBFIRST, SPI_MODE0)
#define VS1053_SDI_SETTING SPISettings(13800000, MSBFIRST, SPI_MODE0)

// setup pins
void VS1053::setup() {
  // turn off sound card
  pinMode(VS1053_XRESET, OUTPUT);
  digitalWrite(VS1053_XRESET, LOW);
  state = Off;

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
  state = Standby;
  digitalWrite(VS1053_XRESET, HIGH);

  // turn down analog
  setVolume(0xfe, 0xfe);

  // set internal speed, SC_MULT=4.5x, SC_ADD=0.0x
  sciWrite(SCI_CLOCKF, 0xc000);

  // simple check to see if the card is responding
  return sciRead(SCI_STATUS) & 0x40;
}


// power off
void VS1053::end() {
  // turn off sound card
  digitalWrite(VS1053_XRESET, LOW);
  state = Off;
}


// close file
void VS1053::stopTrack() {
  // turn down analog
  setVolume(0xfe, 0xfe);

  // done
  audio.close();
}


// prepare sound card for new file
bool VS1053::startTrack() {
  if (!audio) {
    return false;
  }

  // ensure good state
  {
    // cancel playback
    sciWrite(SCI_MODE, SM_SDINEW | SM_CANCEL);

    // get codec specific fill byte
    sciWrite(SCI_WRAMADDR, XP_ENDFILLBYTE);
    uint8_t endFillByte = sciRead(SCI_WRAM);

    // send endFillByte until cancel is accepted
    uint8_t buffer[VS1053_BUFFER_SIZE];
    memset(buffer, endFillByte, VS1053_BUFFER_SIZE);
    while (sciRead(SCI_MODE) & SM_CANCEL) {
      sendData(buffer, VS1053_BUFFER_SIZE);
    }
  }

  // reset decode time
  skippedTime = 0;
  sciWrite(SCI_DECODETIME, 0x00);
  sciWrite(SCI_DECODETIME, 0x00);

  // process header
  uint8_t *buffer;
  uint16_t bytesRead = audio.readHeader(buffer);
  sendData(buffer, bytesRead);

  // turn analog up
  setVolume(0x00, 0x00);

  return true;
}


// transfer music to the sound card
void VS1053::playTrack() {
  // send data until the track is closed
  while (audio) {
    if (state == Playing || state == Rapid) {
      uint8_t *buffer;
      uint16_t bytesRead = audio.readBlock(buffer);
      if (bytesRead == 0) {
        // close the file if there's no more data
        audio.close();
      } else {
        sendData(buffer, bytesRead);
      }
    }
  }
}


// skip the specified number of seconds
void VS1053::skip(int16_t secs) {
  // check if the card will let us jump now
  if (sciRead(SCI_STATUS) & SS_DO_NOT_JUMP) {
    return;
  }

  // get average byterate
  sciWrite(SCI_WRAMADDR, XP_BYTERATE);
  long rate = sciRead(SCI_WRAM);

  // adjust rate based on codec
  if (!audio.isFlac()) {
    rate >>= 2;
  }
  rate <<= 2;

  // update position
  long pos = audio.position() + rate * secs;
  if (audio.seek(pos)) {
    skippedTime += secs;
  }
}


// get approximate track position in seconds
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
  while (len > 0) {
    uint8_t siz = len > 32 ? 32 : len;
    while (!readyForData());
#ifdef SPI_HAS_TRANSACTION
    SPI.beginTransaction(VS1053_SDI_SETTING);
#endif
    fastDigitalWrite(VS1053_XDCS, LOW);

    for (uint8_t i = 0; i < siz; i++) {
      spiwrite(*data++);
    }

    fastDigitalWrite(VS1053_XDCS, HIGH);
#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
    len -= siz;
  }
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

  while (!readyForData());
#ifdef SPI_HAS_TRANSACTION
  SPI.beginTransaction(VS1053_SCI_SETTING);
#endif
  fastDigitalWrite(VS1053_XCS, LOW);

  spiwrite(VS_READ_COMMAND);
  spiwrite(addr);
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
  while (!readyForData());
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
  return fastDigitalRead(VS1053_XDREQ) || (state == Off);
}

inline __attribute__((always_inline))
uint8_t VS1053::spiread() {
  return SPI.transfer(0x00);
}

inline __attribute__((always_inline))
void VS1053::spiwrite(uint8_t c) {
  SPI.transfer(c);
}

