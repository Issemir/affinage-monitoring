#pragma once

#include <Arduino.h>
#include <Adafruit_SHT31.h>

class Sensor {
public:
  bool begin() {
    return sht31.begin(0x44);
  }

  bool read(float& temperature, float& humidity) {
    temperature = sht31.readTemperature();
    humidity = sht31.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      return false;
    }

    if (temperature < -40.0f || temperature > 125.0f) {
      return false;
    }

    if (humidity < 0.0f || humidity > 100.0f) {
      return false;
    }

    return true;
  }

private:
  Adafruit_SHT31 sht31;
};