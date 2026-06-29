#pragma once
#include "wled.h"
#ifdef WLED_DISABLE_MQTT
#error "This user mod requires MQTT to be enabled."
#endif

#define USERMOD_SHT_TYPE_SHT30 0
#define USERMOD_SHT_TYPE_SHT31 1
#define USERMOD_SHT_TYPE_SHT35 2
#define USERMOD_SHT_TYPE_SHT85 3
#define USERMOD_SHT_TYPE_SHT45 4  // SHT45 - obsługiwany bezpośrednio przez Wire (bez biblioteki)

class SHT;

class ShtUsermod : public Usermod
{
  private:
    bool enabled = true;
    bool firstRunDone = false;
    bool initDone = false;
    bool haMqttDiscovery = false;
    bool haMqttDiscoveryDone = false;

    // SHT30/31/35/85 (biblioteka RobTillaart)
    SHT *shtTempHumidSensor = nullptr;
    byte shtType = 0;
    byte unitOfTemp = 0;
    bool shtInitDone = false;
    bool shtReadDataSuccess = false;
    const byte shtI2cAddress = 0x44;
    unsigned long shtLastTimeUpdated = 0;
    bool shtDataRequested = false;
    float shtCurrentTempC = 0.0f;
    float shtCurrentHumidity = 0.0f;

    // ---------- SHT45 — bezpośrednia obsługa Wire ----------
    bool sht45InitDone = false;

    /**
     * Inicjalizacja SHT45 — sprawdź obecność na I2C.
     */
    bool initSht45() {
      Wire.beginTransmission(shtI2cAddress);
      if (Wire.endTransmission() != 0) {
        DEBUG_PRINTF("[%s] SHT45 not found on I2C!\n", _name);
        return false;
      }
      DEBUG_PRINTF("[%s] SHT45 found.\n", _name);
      return true;
    }

    /**
     * Odczyt z SHT45 (komenda 0xFD = High Precision).
     * Zwraca true jeśli odczyt się powiódł.
     */
    bool readSht45() {
      Wire.beginTransmission(shtI2cAddress);
      Wire.write(0xFD);
      if (Wire.endTransmission() != 0) return false;

      delay(10); // SHT45 potrzebuje max 10ms

      if (Wire.requestFrom((uint8_t)shtI2cAddress, (uint8_t)6) != 6) return false;

      uint8_t buf[6];
      for (int i = 0; i < 6; i++) buf[i] = Wire.read();

      if (!sht45CheckCRC(buf[0], buf[1], buf[2])) return false;
      if (!sht45CheckCRC(buf[3], buf[4], buf[5])) return false;

      uint16_t rawTemp = (buf[0] << 8) | buf[1];
      uint16_t rawHumi = (buf[3] << 8) | buf[4];

      shtCurrentTempC    = -45.0f + 175.0f * (rawTemp / 65535.0f);
      shtCurrentHumidity = constrain(-6.0f + 125.0f * (rawHumi / 65535.0f), 0.0f, 100.0f);
      return true;
    }

    /**
     * CRC-8 Sensirion (poly 0x31, init 0xFF).
     */
    bool sht45CheckCRC(uint8_t msb, uint8_t lsb, uint8_t crc) {
      uint8_t c = 0xFF;
      uint8_t d[2] = {msb, lsb};
      for (int b = 0; b < 2; b++) {
        c ^= d[b];
        for (int i = 0; i < 8; i++)
          c = (c & 0x80) ? (c << 1) ^ 0x31 : (c << 1);
      }
      return c == crc;
    }
    // -------------------------------------------------------

    void initShtTempHumiditySensor();
    void cleanupShtTempHumiditySensor();
    void cleanup();
    inline bool isShtReady() {
      return shtType == USERMOD_SHT_TYPE_SHT45 ? sht45InitDone : shtInitDone;
    }
    void publishTemperatureAndHumidityViaMqtt();
    void publishHomeAssistantAutodiscovery();
    void appendDeviceToMqttDiscoveryMessage(JsonDocument& root);

  public:
    static const char _name[];
    static const char _enabled[];
    static const char _shtType[];
    static const char _unitOfTemp[];
    static const char _haMqttDiscovery[];

    void setup();
    void loop();
    void onMqttConnect(bool sessionPresent);
    void appendConfigData();
    void addToConfig(JsonObject &root);
    bool readFromConfig(JsonObject &root);
    void addToJsonInfo(JsonObject& root);

    bool isEnabled() { return enabled; }
    float getTemperature();
    float getTemperatureC() { return roundf(shtCurrentTempC * 10.0f) / 10.0f; }
    float getTemperatureF() { return (getTemperatureC() * 1.8f) + 32.0f; }
    float getHumidity() { return roundf(shtCurrentHumidity * 10.0f) / 10.0f; }
    const char* getUnitString();

    uint16_t getId() { return USERMOD_ID_SHT; }
};
