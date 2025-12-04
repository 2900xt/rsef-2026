#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

// Pin definitions
#define MQ2_PIN A0          // MQ2 analog output pin
#define SD_CS_PIN 53        // SD card chip select pin (default for Mega)

// BME680 sensor
Adafruit_BME680 bme;

// SD card file
File dataFile;

// Timing
unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL = 30000; // 30 seconds in milliseconds

void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial port to connect
  }

  Serial.println(F("BME680 + MQ2 Data Logger"));
  Serial.println(F("========================"));

  // Initialize BME680
  if (!bme.begin()) {
    Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
    while (1); // Halt
  }

  // Set up BME680 oversampling and filter
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320°C for 150 ms

  Serial.println(F("BME680 initialized successfully"));

  // Initialize MQ2 pin
  pinMode(MQ2_PIN, INPUT);
  Serial.println(F("MQ2 sensor initialized"));

  // Initialize SD card
  Serial.print(F("Initializing SD card..."));
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD card initialization failed!"));
    while (1); // Halt
  }
  Serial.println(F("SD card initialized"));

  // Create/open log file and write header
  dataFile = SD.open("datalog.csv", FILE_WRITE);
  if (dataFile) {
    // Check if file is empty (new file)
    if (dataFile.size() == 0) {
      dataFile.println(F("Timestamp(ms),Temperature(C),Pressure(hPa),Humidity(%),Gas(Ohms),MQ2_Raw"));
    }
    dataFile.close();
    Serial.println(F("Log file ready"));
  } else {
    Serial.println(F("Error opening datalog.csv"));
    while (1); // Halt
  }

  Serial.println(F("Setup complete. Starting data logging..."));
  Serial.println();
}

void loop() {
  unsigned long currentTime = millis();

  // Check if it's time to read sensors
  if (currentTime - lastReadTime >= READ_INTERVAL) {
    lastReadTime = currentTime;

    // Read BME680 sensor
    if (!bme.performReading()) {
      Serial.println(F("Failed to perform BME680 reading"));
      return;
    }

    // Read MQ2 sensor (analog value)
    int mq2Value = analogRead(MQ2_PIN);

    // Get sensor values
    float temperature = bme.temperature;
    float pressure = bme.pressure / 100.0; // Convert to hPa
    float humidity = bme.humidity;
    float gasResistance = bme.gas_resistance / 1000.0; // Convert to KOhms

    // Print to Serial Monitor
    Serial.println(F("--- Sensor Reading ---"));
    Serial.print(F("Timestamp: ")); Serial.print(currentTime); Serial.println(F(" ms"));
    Serial.print(F("Temperature: ")); Serial.print(temperature); Serial.println(F(" °C"));
    Serial.print(F("Pressure: ")); Serial.print(pressure); Serial.println(F(" hPa"));
    Serial.print(F("Humidity: ")); Serial.print(humidity); Serial.println(F(" %"));
    Serial.print(F("Gas Resistance: ")); Serial.print(gasResistance); Serial.println(F(" KOhms"));
    Serial.print(F("MQ2 Raw Value: ")); Serial.println(mq2Value);

    // Write to SD card
    dataFile = SD.open("datalog.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.print(currentTime);
      dataFile.print(",");
      dataFile.print(temperature, 2);
      dataFile.print(",");
      dataFile.print(pressure, 2);
      dataFile.print(",");
      dataFile.print(humidity, 2);
      dataFile.print(",");
      dataFile.print(gasResistance, 2);
      dataFile.print(",");
      dataFile.println(mq2Value);
      dataFile.close();

      Serial.println(F("Data written to SD card"));
    } else {
      Serial.println(F("Error opening datalog.csv for writing"));
    }

    Serial.println();
  }
}
