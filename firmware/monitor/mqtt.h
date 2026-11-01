#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

class MqttTransport {
public:
  MqttTransport(
    PubSubClient& clientRef,
    ReadingBuffer& bufferRef
  ) : client(clientRef), buffer(bufferRef) {}

  void begin(
    const char* username,
    const char* password,
    const char* publishTopic
  ) {
    mqttUsername = username;
    mqttPassword = password;
    measurementTopic = publishTopic;

    // Derive the device name from the end of the measurement topic.
    String topic = String(publishTopic);
    int slash = topic.lastIndexOf('/');

    deviceName = slash >= 0
      ? topic.substring(slash + 1)
      : "device";

    ackTopic = "affinage-fridge/ack/" + deviceName;

    activeInstance = this;
    client.setCallback(messageCallback);
  }

  void loop() {
    if (WiFi.status() != WL_CONNECTED) {
      return;
    }

    if (!client.connected()) {
      uint32_t now = millis();

      if (now - lastConnectAttempt < 5000) {
        return;
      }

      lastConnectAttempt = now;
      connect();
      return;
    }

    client.loop();
    publishOldestIfDue();
  }

private:
  PubSubClient& client;
  ReadingBuffer& buffer;

  const char* mqttUsername = nullptr;
  const char* mqttPassword = nullptr;
  const char* measurementTopic = nullptr;

  String deviceName;
  String ackTopic;

  uint32_t lastConnectAttempt = 0;
  uint32_t lastPublishAttempt = 0;
  uint32_t inFlightTimestamp = 0;

  bool publishAttempted = false;

  static constexpr uint32_t RETRY_INTERVAL_MS = 15000;

  static MqttTransport* activeInstance;

  static void messageCallback(
    char* topic,
    byte* payload,
    unsigned int length
  ) {
    if (activeInstance != nullptr) {
      activeInstance->onMessage(topic, payload, length);
    }
  }

  void connect() {
    String clientId =
      "affinage-nodemcu-" + String(ESP.getChipId(), HEX);

    Serial.print(F("Connecting to MQTT..."));

    if (client.connect(
          clientId.c_str(),
          mqttUsername,
          mqttPassword
        )) {

      Serial.println(F("connected"));

      if (client.subscribe(ackTopic.c_str(), 1)) {
        Serial.print(F("Subscribed to "));
        Serial.println(ackTopic);
      } else {
        Serial.println(F("WARNING: Could not subscribe to ACK topic"));
      }

      // The oldest record remains in storage and will be retried.
      inFlightTimestamp = 0;
      publishAttempted = false;

    } else {
      Serial.printf(
        "failed, MQTT state=%d\n",
        client.state()
      );
    }
  }

  void publishOldestIfDue() {
    if (publishAttempted &&
        millis() - lastPublishAttempt < RETRY_INTERVAL_MS) {
      return;
    }

    Reading reading;

    if (!buffer.oldest(reading)) {
      inFlightTimestamp = 0;
      publishAttempted = false;
      return;
    }

    inFlightTimestamp = reading.timestamp;

    String readingId =
      String(ESP.getChipId(), HEX) + "-" +
      String(reading.timestamp);

    char payload[192];

    snprintf(
      payload,
      sizeof(payload),
      "{\"id\":\"%s\",\"timestamp\":%lu,"
      "\"temperature\":%.2f,\"humidity\":%.2f,"
      "\"timestamp_estimated\":%s}",
      readingId.c_str(),
      static_cast<unsigned long>(reading.timestamp),
      reading.temperature,
      reading.humidity,
      reading.timestampEstimated ? "true" : "false"
    );

    // PubSubClient publishes QoS 0. Our separate application-level
    // acknowledgement provides confirmation of the InfluxDB write.
    bool published = client.publish(
      measurementTopic,
      payload,
      false
    );

    if (published) {
      Serial.printf(
        "Published %s; %lu readings queued\n",
        readingId.c_str(),
        static_cast<unsigned long>(buffer.queued())
      );
    } else {
      Serial.println(F("MQTT publish failed"));
    }

    lastPublishAttempt = millis();
    publishAttempted = true;
  }

  void onMessage(
    char* topic,
    byte* payload,
    unsigned int length
  ) {
    if (String(topic) != ackTopic) {
      return;
    }

    // Parse the ID from {"id": "..."} without requiring a JSON library.
    char message[128];

    if (length >= sizeof(message)) {
      return;
    }

    memcpy(message, payload, length);
    message[length] = '\0';

    char* idKey = strstr(message, "\"id\"");

    if (idKey == nullptr) {
      return;
    }

    char* colon = strchr(idKey, ':');

    if (colon == nullptr) {
      return;
    }

    char* value = colon + 1;

    while (*value == ' ' || *value == '\t') {
      value++;
    }

    if (*value != '"') {
      return;
    }

    value++;

    char* closingQuote = strchr(value, '"');

    if (closingQuote == nullptr) {
      return;
    }

    size_t idLength = closingQuote - value;

    if (idLength == 0 || idLength >= 64) {
      return;
    }

    char readingId[64];

    memcpy(readingId, value, idLength);
    readingId[idLength] = '\0';

    String expectedPrefix =
      String(ESP.getChipId(), HEX) + "-";

    if (strncmp(
          readingId,
          expectedPrefix.c_str(),
          expectedPrefix.length()
        ) != 0) {
      return;
    }

    uint32_t acknowledgedTimestamp = strtoul(
      readingId + expectedPrefix.length(),
      nullptr,
      10
    );

    // Only remove the record if this is the outstanding reading.
    if (acknowledgedTimestamp == 0 ||
        acknowledgedTimestamp != inFlightTimestamp) {
      return;
    }

    if (buffer.acknowledge(acknowledgedTimestamp)) {
      Serial.printf(
        "Chalky confirmed reading %s\n",
        readingId
      );

      inFlightTimestamp = 0;
      publishAttempted = false;
    }
  }
};

// One MQTT transport instance is used by this sketch.
MqttTransport* MqttTransport::activeInstance = nullptr;