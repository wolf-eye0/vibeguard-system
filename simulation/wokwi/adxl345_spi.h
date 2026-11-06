#ifndef ADXL345_SPI_H
#define ADXL345_SPI_H

#include <cstdint>

// VibeGuard SPI Pinout (VSPI with CS remapped to GPIO21 to avoid strapping-pin GPIO5)
#define ADXL345_PIN_CS    21
#define ADXL345_PIN_SCK   18
#define ADXL345_PIN_MISO  19
#define ADXL345_PIN_MOSI  23

// ADXL345 Register Map
#define ADXL345_REG_DEVID          0x00
#define ADXL345_REG_BW_RATE        0x2C
#define ADXL345_REG_POWER_CTL      0x2D
#define ADXL345_REG_INT_ENABLE     0x2E
#define ADXL345_REG_DATA_FORMAT    0x31
#define ADXL345_REG_DATAX0         0x32

#define ADXL345_DEVICE_ID          0xE5

// SPI Read/Write Command Masks
#define ADXL345_SPI_READ           0x80
#define ADXL345_SPI_MB             0x40  // Multi-byte burst read

// Scaling Factor: Full resolution mode = 3.9 mg/LSB (0.0039 g/LSB)
#define ADXL345_SCALE_FACTOR       0.0039f

struct RawAccelData {
    int16_t x;
    int16_t y;
    int16_t z;
};

struct AccelGData {
    float x;
    float y;
    float z;
};

class ADXL345_SPI {
public:
    ADXL345_SPI(uint8_t cs_pin = ADXL345_PIN_CS);
    
    // Initialize SPI bus, verify 0xE5 chip ID, configure ±16g full-res @ 800 Hz ODR
    bool begin();
    
    // Read raw 16-bit counts
    bool readRaw(RawAccelData& raw);
    
    // Read calibrated engineering units (in g)
    bool readG(AccelGData& g);
    
    // Low-level register operations
    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);
    void readBurst(uint8_t start_reg, uint8_t* buffer, uint8_t length);

private:
    uint8_t _cs_pin;
};

#endif // ADXL345_SPI_H
