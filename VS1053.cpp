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
  // turn off coproc
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
void VS1053::begin() {
  // turn on coproc
  state = Powerup;
  digitalWrite(VS1053_XRESET, HIGH);
  while (!readyForData());

  // turn down analog
  setVolume(0xfe, 0xfe);

  // set internal speed, SC_MULT=4.5x, SC_ADD=0.0x
  sciWrite(SCI_CLOCKF, 0xc000);

  // wait until ready
  while (!readyForData() && sciRead(SCI_STATUS) != 0x40);

  // max swing
  sciWrite(SCI_STATUS, 0x2141);
}


// power off
void VS1053::end() {
  // turn off coproc
  digitalWrite(VS1053_XRESET, LOW);
  state = Off;
}


// stop playback
void VS1053::stopTrack() {
  // turn down analog
  setVolume(0xfe, 0xfe);

  // done
  audio.close();
}


// begin playback of new file
bool VS1053::startTrack() {
  if (!audio) {
    return false;
  }

  // reset decode time
  skippedTime = 0;
  sciWrite(SCI_DECODETIME, 0x00);
  sciWrite(SCI_DECODETIME, 0x00);

  // process header
  uint16_t bytesRead;
  do {
    uint8_t *buffer;
    bytesRead = audio.readHeader(buffer);
    sendData(buffer, bytesRead);
  } while (bytesRead > 0);

  // turn analog up
  setVolume(0x00, 0x00);

  return true;
}


// play to end of file
void VS1053::playTrack() {
  uint8_t *buffer;

  // send data until the track is closed
  while (audio) {
    if (state == Playing || state == Rapid) {
      uint16_t bytesRead = audio.readBlock(buffer);
      if (bytesRead) {
        sendData(buffer, bytesRead);
      } else {
        // close the file if there's no more data
        audio.close();
      }
    }
  }

  // get codec specific filler
  sciWrite(SCI_WRAMADDR, XP_ENDFILLBYTE);
  uint8_t endFillByte = sciRead(SCI_WRAM);
  buffer = audio.fillBuffer(endFillByte, VS1053_BUFFER_SIZE);

  uint16_t flushCounter = audio.isFlac() ? 384 : 64;
  do {
    sendData(buffer, VS1053_BUFFER_SIZE);
  } while (--flushCounter != 0);

  // flush buffer & repeat cancel until header data is reset
  do {
    sciWrite(SCI_MODE, SM_SDINEW | SM_CANCEL);

    // send endFillByte until cancel is accepted
    uint16_t flushCounter = audio.isFlac() ? 384 : 64;
    do {
      sendData(buffer, VS1053_BUFFER_SIZE);
    } while ((--flushCounter != 0) && (sciRead(SCI_MODE) & SM_CANCEL));
  } while (sciRead(SCI_HDAT1));
}


// skip the specified number of seconds
void VS1053::skip(int16_t secs) {
  // check if the coproc can skip now
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
  rate++;
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


// send data to the coproc
void VS1053::sendData(uint8_t data[], uint16_t len) {
  while (len > 0) {
    while (!readyForData()) {
      if (state == Off) return;
    }

#ifdef SPI_HAS_TRANSACTION
    SPI.beginTransaction(VS1053_SDI_SETTING);
#endif
    fastDigitalWrite(VS1053_XDCS, LOW);

    while (len > 0 && readyForData()) {
      uint8_t chunk = min(VS1053_BUFFER_SIZE, len);
      for (uint8_t i = 0; i < chunk; i++) {
        spiwrite(*data++);
      }
      len -= chunk;
    }

    fastDigitalWrite(VS1053_XDCS, HIGH);
#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
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


// check if coproc can take data
inline __attribute__((always_inline))
bool VS1053::readyForData() {
  return fastDigitalRead(VS1053_XDREQ);
}

inline __attribute__((always_inline))
uint8_t VS1053::spiread() {
  return SPI.transfer(0x00);
}

inline __attribute__((always_inline))
void VS1053::spiwrite(uint8_t c) {
  SPI.transfer(c);
}

