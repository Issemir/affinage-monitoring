
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <LittleFS.h>

Adafruit_SHT31 sht31 = Adafruit_SHT31();

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== NodeMCU Storage Diagnostic ===");

  // Flash information
  Serial.printf("Flash chip ID: 0x%06X\n", ESP.getFlashChipId());
  Serial.printf("Flash chip size: %u bytes\n", ESP.getFlashChipRealSize());
  Serial.printf("Flash configured size: %u bytes\n", ESP.getFlashChipSize());

  // Heap information
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

  // LittleFS information
  Serial.println();
  Serial.println("Initialising LittleFS...");

  if (!LittleFS.begin()) {
    Serial.println("ERROR: LittleFS initialisation failed");
    return;
  }

  Serial.println("LittleFS mounted successfully");

  FSInfo fsInfo;

  if (LittleFS.info(fsInfo)) {
    Serial.printf("Filesystem total: %u bytes\n", fsInfo.totalBytes);
    Serial.printf("Filesystem used: %u bytes\n", fsInfo.usedBytes);
    Serial.printf("Filesystem free: %u bytes\n",
                  fsInfo.totalBytes - fsInfo.usedBytes);
  }

  Serial.println();
  Serial.println("=== Diagnostic Complete ===");

  Serial.println();
  Serial.println("=== SHT31-D Sensor Diagnostic ===");

  // NodeMCU D2 = GPIO4 (SDA)
  // NodeMCU D1 = GPIO5 (SCL)
  Wire.begin(4, 5);

  if (!sht31.begin(0x44)) {
    Serial.println("ERROR: SHT31-D not detected!");
    Serial.println("Check wiring and I2C address.");
    return;
  }

  Serial.println("SHT31-D detected successfully.");
}

void loop() {
  float temperature = sht31.readTemperature();
  float humidity = sht31.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("ERROR: Failed to read sensor.");
  } else {
    Serial.printf("Temperature: %.2f C | Humidity: %.2f %%\n",
                  temperature, humidity);
  }

  delay(3000);
}
