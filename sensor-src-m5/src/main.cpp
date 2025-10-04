#include <M5StickCPlus.h>  // Changed from M5StickC.h
#include <Wire.h>
#include <vector>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME680 bme;


#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
WiFiMulti wifiMulti;
HTTPClient http;
const char *wifi_ssid = "Verizon_T6YMSZ";
const char *server_ip = "http://192.168.1.85:5000/";

const String dev_name = "sensor_01";
const String location = "39.042388, -77.550108";

std::vector<String> readings;

bool is_registered = false;

void API_register() {
  String callpoint = server_ip;
  callpoint += "register";

  Serial.println("[HTTP] register begin...");
  M5.Lcd.println("[HTTP] register begin...");

  http.begin(callpoint);
  http.addHeader("Content-Type", "application/json");

  String postData = "{\"name\":\"" + dev_name + "\", \"location\":\"" + location + "\"}";

  Serial.println("[HTTP] register POST...");
  M5.Lcd.println("[HTTP] register POST...");

  int httpCode = http.POST(postData);

  if (httpCode != 200) {
    Serial.printf("POST register failed: %s\n", http.errorToString(httpCode).c_str());
    M5.Lcd.printf("Reg FAIL: %s\n", http.errorToString(httpCode).c_str());
  } else {
    Serial.printf("POST register OK: code %d\n", httpCode);
    M5.Lcd.printf("Reg OK: %d\n", httpCode);
    is_registered = true;
    String response = http.getString();
  }

  http.end();
}


void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);  // Larger text for bigger screen
  M5.Lcd.setTextColor(WHITE);
  
  Wire.begin(32, 33);  // Grove port: SDA=32, SCL=33
  
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.println("BME680 Test");
  
  if (!bme.begin(0x77)) {  // Try 0x77 if this fails
    M5.Lcd.println("No BME680!");
    M5.Lcd.println("Check wiring");
    while (1) delay(10);
  }
  
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);
  
  M5.Lcd.println("Ready!");
  delay(1000);
}

void loop() {
  if (!bme.performReading()) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 10);
    M5.Lcd.println("Reading failed");
    delay(2000);
    return;
  }
  
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 10);
  
  M5.Lcd.printf("Temp: %.1f C\n\n", bme.temperature);
  M5.Lcd.printf("Hum:  %.1f %%\n\n", bme.humidity);
  M5.Lcd.printf("Press: %.0f hPa\n\n", bme.pressure / 100.0);
  M5.Lcd.printf("Gas: %.1f KOhm", bme.gas_resistance / 1000.0);
  
  delay(2000);
}