#include "ShtUsermod.h"
#include "SHT85.h"

// Strings to reduce flash memory usage (used more than twice)
const char ShtUsermod::_name[]            PROGMEM = "SHT-Sensor";
const char ShtUsermod::_enabled[]         PROGMEM = "Enabled";
const char ShtUsermod::_shtType[]         PROGMEM = "SHT-Type";
const char ShtUsermod::_unitOfTemp[]      PROGMEM = "Unit";
const char ShtUsermod::_haMqttDiscovery[] PROGMEM = "Add-To-HA-MQTT-Discovery";

void ShtUsermod::initShtTempHumiditySensor()
{
  // SHT45 nie używa biblioteki RobTillaart — obsługiwany przez Wire w ShtUsermod.h
  if (shtType == USERMOD_SHT_TYPE_SHT45) {
    sht45InitDone = initSht45();
    if (!sht45InitDone) cleanup();
    return;
  }

  switch (shtType) {
    case USERMOD_SHT_TYPE_SHT30: shtTempHumidSensor = (SHT *) new SHT30(); break;
    case USERMOD_SHT_TYPE_SHT31: shtTempHumidSensor = (SHT *) new SHT31(); break;
    case USERMOD_SHT_TYPE_SHT35: shtTempHumidSensor = (SHT *) new SHT35(); break;
    case USERMOD_SHT_TYPE_SHT85: shtTempHumidSensor = (SHT *) new SHT85(); break;
  }

  shtTempHumidSensor->begin(shtI2cAddress);
  if (shtTempHumidSensor->readStatus() == 0xFFFF) {
    DEBUG_PRINTF("[%s] SHT init failed!\n", _name);
    cleanup();
    return;
  }

  shtInitDone = true;
}

void ShtUsermod::cleanupShtTempHumiditySensor()
{
  if (shtType == USERMOD_SHT_TYPE_SHT45) {
    sht45InitDone = false;
    return;
  }

  if (isShtReady()) {
    shtTempHumidSensor->reset();
    delete shtTempHumidSensor;
    shtTempHumidSensor = nullptr;
  }
  shtInitDone = false;
}

void ShtUsermod::cleanup()
{
  cleanupShtTempHumiditySensor();
  enabled = false;
}

void ShtUsermod::publishTemperatureAndHumidityViaMqtt() {
  if (!WLED_MQTT_CONNECTED) return;
  char buf[128];

  snprintf_P(buf, 127, PSTR("%s/temperature"), mqttDeviceTopic);
  mqtt->publish(buf, 0, false, String(getTemperature()).c_str());
  snprintf_P(buf, 127, PSTR("%s/humidity"), mqttDeviceTopic);
  mqtt->publish(buf, 0, false, String(getHumidity()).c_str());
}

void ShtUsermod::publishHomeAssistantAutodiscovery() {
  if (!WLED_MQTT_CONNECTED) return;

  char json_str[1024], buf[128];
  size_t payload_size;
  StaticJsonDocument<1024> json;

  snprintf_P(buf, 127, PSTR("%s Temperature"), serverDescription);
  json[F("name")] = buf;
  snprintf_P(buf, 127, PSTR("%s/temperature"), mqttDeviceTopic);
  json[F("stat_t")] = buf;
  json[F("dev_cla")] = F("temperature");
  json[F("stat_cla")] = F("measurement");
  snprintf_P(buf, 127, PSTR("%s-temperature"), escapedMac.c_str());
  json[F("uniq_id")] = buf;
  json[F("unit_of_meas")] = unitOfTemp ? F("°F") : F("°C");
  appendDeviceToMqttDiscoveryMessage(json);
  payload_size = serializeJson(json, json_str);
  snprintf_P(buf, 127, PSTR("homeassistant/sensor/%s/%s-temperature/config"), escapedMac.c_str(), escapedMac.c_str());
  mqtt->publish(buf, 0, true, json_str, payload_size);

  json.clear();

  snprintf_P(buf, 127, PSTR("%s Humidity"), serverDescription);
  json[F("name")] = buf;
  snprintf_P(buf, 127, PSTR("%s/humidity"), mqttDeviceTopic);
  json[F("stat_t")] = buf;
  json[F("dev_cla")] = F("humidity");
  json[F("stat_cla")] = F("measurement");
  snprintf_P(buf, 127, PSTR("%s-humidity"), escapedMac.c_str());
  json[F("uniq_id")] = buf;
  json[F("unit_of_meas")] = F("%");
  appendDeviceToMqttDiscoveryMessage(json);
  payload_size = serializeJson(json, json_str);
  snprintf_P(buf, 127, PSTR("homeassistant/sensor/%s/%s-humidity/config"), escapedMac.c_str(), escapedMac.c_str());
  mqtt->publish(buf, 0, true, json_str, payload_size);

  haMqttDiscoveryDone = true;
}

void ShtUsermod::appendDeviceToMqttDiscoveryMessage(JsonDocument& root) {
  JsonObject device = root.createNestedObject(F("dev"));
  device[F("ids")] = escapedMac.c_str();
  device[F("name")] = serverDescription;
  device[F("sw")] = versionString;
  #ifdef ARDUINO_ARCH_ESP32
  device[F("mdl")] = ESP.getChipModel();
  #else
  device[F("mdl")] = F("ESP8266");
  #endif
  device[F("mf")] = F("espressif");
}

void ShtUsermod::setup()
{
  if (enabled) {
    if (i2c_sda < 0 || i2c_scl < 0) {
      DEBUG_PRINTF("[%s] I2C bus not initialised!\n", _name);
      cleanup();
      return;
    }

    initShtTempHumiditySensor();
    initDone = true;
  }

  firstRunDone = true;
}

void ShtUsermod::loop()
{
  if (!enabled || !initDone || strip.isUpdating()) return;

  if (shtType == USERMOD_SHT_TYPE_SHT45) {
    // SHT45: synchroniczny odczyt co 30s
    if (!sht45InitDone) return;
    if (millis() - shtLastTimeUpdated < 30000) return;
    shtLastTimeUpdated = millis();

    if (readSht45()) {
      publishTemperatureAndHumidityViaMqtt();
      shtReadDataSuccess = true;
    } else {
      DEBUG_PRINTF("[%s] SHT45 read failed!\n", _name);
      shtReadDataSuccess = false;
    }
    return;
  }

  // SHT30/31/35/85: asynchroniczny odczyt (oryginalny kod)
  if (isShtReady()) {
    if (millis() - shtLastTimeUpdated > 30000 && !shtDataRequested) {
      shtTempHumidSensor->requestData();
      shtDataRequested = true;
      shtLastTimeUpdated = millis();
    }

    if (shtDataRequested) {
      if (shtTempHumidSensor->dataReady()) {
        if (shtTempHumidSensor->readData(false)) {
          shtCurrentTempC = shtTempHumidSensor->getTemperature();
          shtCurrentHumidity = shtTempHumidSensor->getHumidity();
          publishTemperatureAndHumidityViaMqtt();
          shtReadDataSuccess = true;
        } else {
          shtReadDataSuccess = false;
        }
        shtDataRequested = false;
      }
    }
  }
}

void ShtUsermod::onMqttConnect(bool sessionPresent) {
  if (haMqttDiscovery && !haMqttDiscoveryDone) publishHomeAssistantAutodiscovery();
}

void ShtUsermod::appendConfigData() {
  oappend(F("dd=addDropdown('"));
  oappend(_name);
  oappend(F("','"));
  oappend(_shtType);
  oappend(F("');"));
  oappend(F("addOption(dd,'SHT30',0);"));
  oappend(F("addOption(dd,'SHT31',1);"));
  oappend(F("addOption(dd,'SHT35',2);"));
  oappend(F("addOption(dd,'SHT85',3);"));
  oappend(F("addOption(dd,'SHT45',4);"));  // <-- nowe
  oappend(F("dd=addDropdown('"));
  oappend(_name);
  oappend(F("','"));
  oappend(_unitOfTemp);
  oappend(F("');"));
  oappend(F("addOption(dd,'Celsius',0);"));
  oappend(F("addOption(dd,'Fahrenheit',1);"));
}

void ShtUsermod::addToConfig(JsonObject &root)
{
  JsonObject top = root.createNestedObject(FPSTR(_name));
  top[FPSTR(_enabled)] = enabled;
  top[FPSTR(_shtType)] = shtType;
  top[FPSTR(_unitOfTemp)] = unitOfTemp;
  top[FPSTR(_haMqttDiscovery)] = haMqttDiscovery;
}

bool ShtUsermod::readFromConfig(JsonObject &root)
{
  JsonObject top = root[FPSTR(_name)];
  if (top.isNull()) {
    DEBUG_PRINTF("[%s] No config found. (Using defaults.)\n", _name);
    return false;
  }

  bool oldEnabled = enabled;
  byte oldShtType = shtType;
  byte oldUnitOfTemp = unitOfTemp;
  bool oldHaMqttDiscovery = haMqttDiscovery;

  getJsonValue(top[FPSTR(_enabled)], enabled);
  getJsonValue(top[FPSTR(_shtType)], shtType);
  getJsonValue(top[FPSTR(_unitOfTemp)], unitOfTemp);
  getJsonValue(top[FPSTR(_haMqttDiscovery)], haMqttDiscovery);

  if (!firstRunDone) {
    DEBUG_PRINTF("[%s] First run, nothing to do\n", _name);
  } else if (enabled != oldEnabled) {
    enabled ? setup() : cleanup();
    DEBUG_PRINTF("[%s] Usermod has been en-/disabled\n", _name);
  } else if (enabled) {
    if (oldShtType != shtType) {
      cleanupShtTempHumiditySensor();
      initShtTempHumiditySensor();
    }
    if (oldUnitOfTemp != unitOfTemp) {
      publishTemperatureAndHumidityViaMqtt();
      publishHomeAssistantAutodiscovery();
    }
    if (oldHaMqttDiscovery != haMqttDiscovery && haMqttDiscovery) {
      publishHomeAssistantAutodiscovery();
    }
    DEBUG_PRINTF("[%s] Config (re)loaded\n", _name);
  }

  return true;
}

void ShtUsermod::addToJsonInfo(JsonObject& root)
{
  if (!enabled && !isShtReady()) return;

  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonArray jsonTemp = user.createNestedArray(F("Temperature"));
  JsonArray jsonHumidity = user.createNestedArray(F("Humidity"));

  if (shtLastTimeUpdated == 0 || !shtReadDataSuccess) {
    jsonTemp.add(0);
    jsonHumidity.add(0);
    jsonTemp.add(shtLastTimeUpdated == 0 ? F(" Not read yet") : F(" Error"));
    jsonHumidity.add(shtLastTimeUpdated == 0 ? F(" Not read yet") : F(" Error"));
    return;
  }

  jsonHumidity.add(getHumidity());
  jsonHumidity.add(F(" RH"));
  jsonTemp.add(getTemperature());
  jsonTemp.add(getUnitString());

  JsonObject sensor = root[F("sensor")];
  if (sensor.isNull()) sensor = root.createNestedObject(F("sensor"));

  jsonTemp = sensor.createNestedArray(F("temp"));
  jsonTemp.add(getTemperature());
  jsonTemp.add(getUnitString());

  jsonHumidity = sensor.createNestedArray(F("humidity"));
  jsonHumidity.add(getHumidity());
  jsonHumidity.add(F(" RH"));
}

float ShtUsermod::getTemperature() {
  return unitOfTemp ? getTemperatureF() : getTemperatureC();
}

const char* ShtUsermod::getUnitString() {
  return unitOfTemp ? "°F" : "°C";
}

static ShtUsermod sht;
REGISTER_USERMOD(sht);
