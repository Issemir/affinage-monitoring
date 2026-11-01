#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <time.h>

#include "secrets.h"
#include "sensor.h"
#include "storage.h"
#include "buffer.h"
#include "mqtt.h"

// Wi-Fi credentials remain in your existing secrets.h.
const char* WIFI_SSID = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;

Sensor sensor;
Storage storage;
ReadingBuffer buffer(storage);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
MqttTransport transport(mqttClient, buffer);

uint32_t lastSampleMs = 0;
uint32_t lastWifiAttemptMs = 0;

void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lastWifiAttemptMs = millis();
  Serial.println(F("Connecting to Wi-Fi..."));
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=== Affinage Monitor ==="));

  // SHT31: SDA = D2/GPIO4, SCL = D1/GPIO5.
  Wire.begin(4, 5);

  if (!sensor.begin()) {
    Serial.println(F("ERROR: SHT31 sensor not detected"));
    while (true) delay(1000);
  }

  Serial.println(F("SHT31 sensor initialised"));

  if (!storage.begin()) {
    Serial.println(F("ERROR: LittleFS initialisation failed"));
    while (true) delay(1000);
  }

  buffer.begin();

  Serial.printf("Storage free: %u bytes\n", storage.freeBytes());
  Serial.printf("Queued readings: %u\n", buffer.queued());

  mqttClient.setServer(SECRET_MQTT_SERVER, SECRET_MQTT_PORT);
  mqttClient.setBufferSize(256);

  transport.begin(
    SECRET_MQTT_USER,
    SECRET_MQTT_PASSWORD,
    SECRET_MQTT_TOPIC
  );

  // UTC timestamps; Grafana can handle local-time presentation.
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  startWiFi();

  Serial.println(F("Initialisation complete"));
}

void loop() {
  uint32_t now = millis();

  // Reconnect Wi-Fi periodically without blocking the sampling loop.
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
      startWiFi();
    }
  } else {
    transport.loop();
  }

  // The sampling interval increases as the storage queue fills.
  const uint32_t sampleInterval = buffer.samplingIntervalMs();

  if (now - lastSampleMs >= sampleInterval) {
    lastSampleMs = now;

    float temperature;
    float humidity;

    if (sensor.read(temperature, humidity)) {
      const time_t systemTime = time(nullptr);
      const bool ntpAvailable = systemTime > 1700000000;

      bool timestampEstimated = false;

      const uint32_t timestamp = buffer.nextTimestamp(
        ntpAvailable ? static_cast<uint32_t>(systemTime) : 0,
        timestampEstimated,
        sampleInterval / 1000
      );

      Reading reading;
      reading.timestamp = timestamp;
      reading.temperature = temperature;
      reading.humidity = humidity;
      reading.timestampEstimated = timestampEstimated;

      if (buffer.enqueue(reading)) {
        Serial.printf(
          "Queued: %lu | %.2f C | %.2f%% RH%s\n",
          static_cast<unsigned long>(timestamp),
          temperature,
          humidity,
          timestampEstimated ? " | ESTIMATED TIME" : ""
        );
      } else {
        Serial.println(F("ERROR: Failed to save reading"));
      }
    } else {
      Serial.println(F("ERROR: Failed to read SHT31"));
    }
  }

  // Keep MQTT responsive.
  if (WiFi.status() == WL_CONNECTED) {
    transport.loop();
  }

  yield();
}