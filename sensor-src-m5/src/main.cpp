#include <config.h>

#include <M5StickCPlus.h>
#include <Wire.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
Adafruit_BME680 bme;

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
WiFiMulti wifiMulti;
HTTPClient http;
const char *server_ip = "http://192.168.1.190:5000/";
const String dev_name = "sensor_02";
const String location = "39.042388, -77.550108";
bool is_registered = false;
unsigned long lastUpdate = 0;

struct SensorData
{
    float temperature;
    float humidity;
    float pressure;
    float gasResistance;
    unsigned long timestamp;
};

SensorData currentReading;

void API_register()
{
    Serial.println("Registering with server...");
    String callpoint = server_ip;
    callpoint += "register";

    http.begin(callpoint);
    http.addHeader("Content-Type", "application/json");

    String postData = "{\"name\":\"" + dev_name + "\", \"location\":\"" + location + "\"}";

    int httpCode = http.POST(postData);
    if (httpCode == 200)
    {
        is_registered = true;
        Serial.println("Registration successful");
    }
    else
    {
        Serial.printf("Registration failed: HTTP %d\n", httpCode);
    }

    http.end();
}

void API_update()
{
    static int fail_count = 0;
    String callpoint = server_ip;
    callpoint += "update";

    http.begin(callpoint);
    http.addHeader("Content-Type", "application/json");

    String postData = "{\"name\":\"" + dev_name + "\", \"temperature\":" + String(currentReading.temperature) +
                      ", \"humidity\":" + String(currentReading.humidity) +
                      ", \"pressure\":" + String(currentReading.pressure) +
                      ", \"gasResistance\":" + String(currentReading.gasResistance) + "}";

    int httpCode = http.POST(postData);
    if (httpCode != 200)
    {
        Serial.printf("API update failed: HTTP %d\n", httpCode);
        fail_count++;
        if (fail_count > 5)
        {
            is_registered = false;
            fail_count = 0;
            Serial.println("Too many failures, re-registering...");
        }
    }
    else
    {
        Serial.println("Data sent successfully");
        fail_count = 0;
    }

    http.end();
}

void setup()
{
    M5.begin();
    Serial.begin(115200);

    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0, 0);

    Serial.println("\n=== CROPSENSE INIT ===");
    M5.Lcd.println("CROPSENSE");

    Serial.println("Connecting to WiFi...");
    M5.Lcd.println("WiFi...");
    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWD);

    if (wifiMulti.run() == WL_CONNECTED)
    {
        Serial.println("WiFi connected");
        M5.Lcd.println("WiFi OK");
    }
    else
    {
        Serial.println("WiFi failed");
        M5.Lcd.println("WiFi FAIL");
    }

    Wire.begin(32, 33);

    Serial.println("Initializing BME680...");
    M5.Lcd.println("BME680...");
    if (!bme.begin(0x77))
    {
        Serial.println("BME680 ERROR!");
        M5.Lcd.println("BME680 ERR");
        while (1);
    }

    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);

    Serial.println("BME680 ready");
    M5.Lcd.println("BME680 OK");

    Serial.println("=== INIT COMPLETE ===\n");

    lastUpdate = millis();
}

void loop()
{
    unsigned long currentTime = millis();

    if (currentTime - lastUpdate < 15000)
    {
        delay(100);
        return;
    }

    Serial.println("\n--- Reading Sensors ---");

    if (!bme.performReading())
    {
        Serial.println("ERROR: BME680 read failed");
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("BME680 ERROR");
        delay(1000);
        return;
    }

    currentReading.temperature = bme.temperature;
    currentReading.humidity = bme.humidity;
    currentReading.pressure = bme.pressure / 1000.0;
    currentReading.gasResistance = bme.gas_resistance / 1000.0;

    currentReading.timestamp = currentTime;

    Serial.printf("Temp: %.1fC\n", currentReading.temperature);
    Serial.printf("Humidity: %.1f%%\n", currentReading.humidity);
    Serial.printf("Pressure: %.2f kPa\n", currentReading.pressure);
    Serial.printf("Gas: %.2f K\n", currentReading.gasResistance);

    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("T:%.1fC H:%.0f%%\n", currentReading.temperature, currentReading.humidity);
    M5.Lcd.printf("P:%.1f G:%.1f\n", currentReading.pressure, currentReading.gasResistance);
    if (!is_registered)
    {
        API_register();
    }
    else
    {
        API_update();
    }

    lastUpdate = currentTime;
}