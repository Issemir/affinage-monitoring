
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_SHT31.h>

// Wi-Fi settings
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Chalky: MQTT broker
const char* MQTT_SERVER = "192.168.0.252";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "YOUR_MQTT_USERNAME";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";

// Match the topic subscribed to by Telegraf
const char* MQTT_TOPIC = "affinage-fridge/measurements/nodemcu";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Adafruit_SHT31 sht31 = Adafruit_SHT31();

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 30000;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wi-Fi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");

    String clientId = "affinage-nodemcu-" +
                      String(ESP.getChipId(), HEX);

    if (mqtt.connect(clientId.c_str(), MQTT_USER,
                     MQTT_PASSWORD)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, state=");
      Serial.println(mqtt.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Wire.begin(4, 5); // SDA = D2, SCL = D1

  if (!sht31.begin(0x44)) {
    Serial.println("ERROR: SHT31-D not detected");
    while (true) delay(1000);
  }

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);

  connectWiFi();
  connectMQTT();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqtt.connected()) {
    connectMQTT();
  }

  mqtt.loop();

  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();

    float temperature = sht31.readTemperature();
    float humidity = sht31.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("ERROR: Failed to read sensor");
      return;
    }

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f,\"humidity\":%.2f}",
             temperature, humidity);

    if (mqtt.publish(MQTT_TOPIC, payload)) {
      Serial.print("Published: ");
      Serial.println(payload);
    } else {
      Serial.println("MQTT publish failed");
    }
  }
}
