
#include <Wire.h>
#include <Adafruit_SHT31.h>

Adafruit_SHT31 sht31 = Adafruit_SHT31();

void setup() {
  Serial.begin(115200);
  delay(1500);

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
