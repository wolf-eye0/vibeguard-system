#include "adxl345_spi.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>

ADXL345_SPI::ADXL345_SPI(uint8_t cs_pin) : _cs_pin(cs_pin) {}

bool ADXL345_SPI::begin() {
    pinMode(_cs_pin, OUTPUT);
    digitalWrite(_cs_pin, HIGH);

    // Initialize SPI at 5 MHz, MSBFIRST, SPI_MODE3 (CPOL=1, CPHA=1 as per ADXL345 datasheet)
    SPI.begin(ADXL345_PIN_SCK, ADXL345_PIN_MISO, ADXL345_PIN_MOSI, _cs_pin);
    SPI.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE3));

    delay(10);

    // Verify Device ID (Register 0x00 should return 0xE5)
    uint8_t dev_id = readRegister(ADXL345_REG_DEVID);
    if (dev_id != ADXL345_DEVICE_ID) {
        // Fallback: Retry with SPI_MODE0 in case of level shifter inversion
        SPI.endTransaction();
        SPI.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE0));
        dev_id = readRegister(ADXL345_REG_DEVID);
        if (dev_id != ADXL345_DEVICE_ID) {
            return false;
        }
    }

    // Configure 800 Hz ODR (BW_RATE = 0x0D -> 800 Hz, normal power)
    writeRegister(ADXL345_REG_BW_RATE, 0x0D);

    // Configure Full-Resolution Mode, ±16g range, 4-wire SPI (DATA_FORMAT = 0x0B)
    writeRegister(ADXL345_REG_DATA_FORMAT, 0x0B);

    // Set Measurement Bit (POWER_CTL = 0x08 -> Measurement mode enabled)
    writeRegister(ADXL345_REG_POWER_CTL, 0x08);

    return true;
}

uint8_t ADXL345_SPI::readRegister(uint8_t reg) {
    digitalWrite(_cs_pin, LOW);
    SPI.transfer(reg | ADXL345_SPI_READ);
    uint8_t val = SPI.transfer(0x00);
    digitalWrite(_cs_pin, HIGH);
    return val;
}

void ADXL345_SPI::writeRegister(uint8_t reg, uint8_t value) {
    digitalWrite(_cs_pin, LOW);
    SPI.transfer(reg & ~ADXL345_SPI_READ);
    SPI.transfer(value);
    digitalWrite(_cs_pin, HIGH);
}

void ADXL345_SPI::readBurst(uint8_t start_reg, uint8_t* buffer, uint8_t length) {
    digitalWrite(_cs_pin, LOW);
    SPI.transfer(start_reg | ADXL345_SPI_READ | ADXL345_SPI_MB);
    for (uint8_t i = 0; i < length; ++i) {
        buffer[i] = SPI.transfer(0x00);
    }
    digitalWrite(_cs_pin, HIGH);
}

bool ADXL345_SPI::readRaw(RawAccelData& raw) {
    uint8_t buf[6];
    readBurst(ADXL345_REG_DATAX0, buf, 6);
    raw.x = static_cast<int16_t>(buf[0] | (buf[1] << 8));
    raw.y = static_cast<int16_t>(buf[2] | (buf[3] << 8));
    raw.z = static_cast<int16_t>(buf[4] | (buf[5] << 8));
    return true;
}

bool ADXL345_SPI::readG(AccelGData& g) {
    RawAccelData raw;
    if (!readRaw(raw)) return false;
    g.x = static_cast<float>(raw.x) * ADXL345_SCALE_FACTOR;
    g.y = static_cast<float>(raw.y) * ADXL345_SCALE_FACTOR;
    g.z = static_cast<float>(raw.z) * ADXL345_SCALE_FACTOR;
    return true;
}

#else
// Host Mock Implementation for Standalone Linux Compilation / Testing
ADXL345_SPI::ADXL345_SPI(uint8_t cs_pin) : _cs_pin(cs_pin) {}
bool ADXL345_SPI::begin() { return true; }
uint8_t ADXL345_SPI::readRegister(uint8_t reg) { return (reg == ADXL345_REG_DEVID) ? 0xE5 : 0x00; }
void ADXL345_SPI::writeRegister(uint8_t reg, uint8_t value) {}
void ADXL345_SPI::readBurst(uint8_t start_reg, uint8_t* buffer, uint8_t length) {
    for (uint8_t i = 0; i < length; ++i) buffer[i] = 0;
}
bool ADXL345_SPI::readRaw(RawAccelData& raw) { raw = {0, 0, 256}; return true; }
bool ADXL345_SPI::readG(AccelGData& g) { g = {0.0f, 0.0f, 1.0f}; return true; }
#endif
