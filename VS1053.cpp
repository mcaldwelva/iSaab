/*
 *  VS1053 provides a high-speed codec interface for general playback
 *         of an audio file in any vendor supported format
 *
 */

#include <SPI.h>
#include <SD.h>
#include "VS1053.h"

#define VS1053_SCI_SETTING SPISettings(12288000/7, MSBFIRST, SPI_MODE0)
#define VS1053_SDI_SETTING SPISettings(55296000/4, MSBFIRST, SPI_MODE0)

// setup pins
void VS1053::setup() {
  // turn off codec
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
  // turn on codec
  digitalWrite(VS1053_XRESET, HIGH);
  while (!readyForData());

  // turn down analog
  setVolume(0xfe, 0xfe);

  // max internal speed
  sciWrite(SCI_CLOCKF, 0xc000);

  // wait until ready
  while (!readyForData() && sciRead(SCI_STATUS) != 0x40);

  // max swing
  sciWrite(SCI_STATUS, 0x2141);
}


// power off
void VS1053::end() {
  // turn off codec
  digitalWrite(VS1053_XRESET, LOW);
}


// stop playback
void VS1053::stopTrack() {
  // turn down analog
  setVolume(0xfe, 0xfe);

  // done
  audio.close();
}


// play to end of file
void VS1053::playTrack() {
  uint8_t *buffer;
  int bytesRead;

  if (!audio) {
    return;
  }

  // wait up to 15ms for HDAT to clear
  for (uint8_t j = 15; j > 0 && sciRead(SCI_HDAT1); j--) {
    delay(1);
  }

  // reset decode time
  skippedTime = 0;
  sciWrite(SCI_DECODETIME, 0x00);
  sciWrite(SCI_DECODETIME, 0x00);

  // process metadata
  do {
    bytesRead = audio.readMetadata(buffer);
    sendData(buffer, bytesRead);
  } while (bytesRead > 0);

  // turn analog up
  setVolume(0x00, 0x00);

  // send data until the track is closed
  while (audio) {
    bytesRead = audio.readBlock(buffer);

    if (bytesRead > 0) {
      sendData(buffer, bytesRead);
    } else {
      audio.close();
    }
  }

  // get codec specific filler
  sciWrite(SCI_WRAMADDR, XP_ENDFILLBYTE);
  uint8_t endFillByte = sciRead(SCI_WRAM);
  buffer = audio.fillBuffer(endFillByte, VS1053_BUFFER_SIZE);

  // flush buffer
  uint16_t i = audio.isHighBitRate() ? 384 : 64;
  do {
    sendData(buffer, VS1053_BUFFER_SIZE);
  } while (--i != 0);

  // cancel playback
  sciWrite(SCI_MODE, SM_SDINEW | SM_CANCEL);

  // send endFillByte until cancel is accepted
  i = audio.isHighBitRate() ? 384 : 64;
  do {
    sendData(buffer, VS1053_BUFFER_SIZE);
  } while ((--i != 0) && (sciRead(SCI_MODE) & SM_CANCEL));
}


// skip the specified number of seconds
void VS1053::skip(int16_t secs) {
  // check if the codec can skip now
  if (sciRead(SCI_STATUS) & SS_DO_NOT_JUMP) {
    return;
  }

  // get average byterate
  sciWrite(SCI_WRAMADDR, XP_BYTERATE);
  uint16_t rate = sciRead(SCI_WRAM);

  // jump to new location
  if (audio.jump(secs, rate)) {
    skippedTime += secs;
  }
}


// get approximate track position in seconds
uint16_t VS1053::trackTime() {
  uint16_t ret;

  if (audio) {
    ret = sciRead(SCI_DECODETIME) + skippedTime;
  } else {
    ret = 0;
  }

  return ret;
}


// load a patch or plugin from disk
bool VS1053::loadPlugin(const __FlashStringHelper* fileName) {
  uint8_t buff[2];
  uint16_t addr, count, val;

  File plugin = SD.open(fileName);
  while (plugin.available()) {
    plugin.read(buff, 2);
    addr = buff[0];

    plugin.read(buff, 2);
    count = LE8x2(buff);

    if (count & 0x8000) {
      // RLE run, replicate n samples
      count &= 0x7FFF;
      plugin.read(buff, 2);
      val = LE8x2(buff);
      while (count--) {
        sciWrite(addr, val);
      }
    } else {
      // Copy run, copy n samples
      while (count--) {
        plugin.read(buff, 2);
        val = LE8x2(buff);
        sciWrite(addr, val);
      }
    }
  }

  plugin.close();
}


// send data to the codec
void VS1053::sendData(uint8_t data[], uint16_t len) {
  while (len > 0) {
    while (!readyForData() || state == Paused);

    SPI.beginTransaction(VS1053_SDI_SETTING);
    fastDigitalWrite(VS1053_XDCS, LOW);

    while (len > 0 && readyForData()) {
      uint8_t chunk = min(VS1053_BUFFER_SIZE, len);
      for (uint8_t i = 0; i < chunk; i++) {
        spiwrite(*data++);
      }
      len -= chunk;
    }

    fastDigitalWrite(VS1053_XDCS, HIGH);
    SPI.endTransaction();
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

  SPI.beginTransaction(VS1053_SCI_SETTING);
  fastDigitalWrite(VS1053_XCS, LOW);

  spiwrite(VS_READ_COMMAND);
  spiwrite(addr);
  delayMicroseconds(10);
  data = spiread();
  data <<= 8;
  data |= spiread();

  fastDigitalWrite(VS1053_XCS, HIGH);
  SPI.endTransaction();

  return data;
}


void VS1053::sciWrite(uint8_t addr, uint16_t data) {
  SPI.beginTransaction(VS1053_SCI_SETTING);
  fastDigitalWrite(VS1053_XCS, LOW);

  spiwrite(VS_WRITE_COMMAND);
  spiwrite(addr);
  spiwrite(data >> 8);
  spiwrite(data);

  fastDigitalWrite(VS1053_XCS, HIGH);
  SPI.endTransaction();
}


// check if codec can take data
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
