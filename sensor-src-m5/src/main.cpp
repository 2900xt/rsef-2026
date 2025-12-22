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
const char *server_ip = "http://192.168.1.235:5000/";
const String dev_name = "sensor_02";
const String location = "39.042388, -77.550108";
bool is_registered = false;
unsigned long lastUpdate = 0;

// MQ2 Gas Sensor Configuration
const int MQ2_PIN = 36;              // GPIO36 (ADC1_CH0)
const float RL_VALUE = 5.0;          // Load resistance in K ohms
const float VCC = 3.3;               // Supply voltage for ESP32
const int ADC_RESOLUTION = 4095;     // 12-bit ADC
const int CALIBRATION_SAMPLES = 50;  // Number of samples for R0 calibration
const float RO_CLEAN_AIR_FACTOR = 9.83; // Rs/R0 ratio in clean air from datasheet

// MQ2 Calibration and Runtime Variables
float MQ2_R0 = 10.0;                 // Sensor resistance in clean air (will be calibrated)
bool MQ2_calibrated = false;

struct SensorData
{
    float temperature;
    float humidity;
    float pressure;
    float gasResistance;
    float mq2_rs;           // MQ2 sensor resistance in K ohms
    float mq2_ratio;        // Rs/R0 ratio
    float mq2_smoke_ppm;    // Estimated smoke concentration in PPM
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
                      ", \"gasResistance\":" + String(currentReading.gasResistance) +
                      ", \"mq2_rs\":" + String(currentReading.mq2_rs) +
                      ", \"mq2_ratio\":" + String(currentReading.mq2_ratio) +
                      ", \"mq2_smoke_ppm\":" + String(currentReading.mq2_smoke_ppm) + "}";

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

// MQ2 Helper Functions

/**
 * Reads the MQ2 sensor resistance (Rs) in K ohms
 */
float MQ2_readRs()
{
    int rawADC = analogRead(MQ2_PIN);

    // Debug output
    Serial.printf("[MQ2 DEBUG] Raw ADC: %d\n", rawADC);

    // Prevent division by zero
    if (rawADC == 0)
    {
        Serial.println("[MQ2 DEBUG] ADC is 0 - sensor not connected or no voltage");
        rawADC = 1;
    }

    // Calculate sensor resistance: Rs = RL * (VCC/VRL - 1)
    // VRL = rawADC * VCC / ADC_RESOLUTION
    // Simplifies to: Rs = RL * (ADC_RESOLUTION / rawADC - 1)
    float rs = RL_VALUE * ((float)ADC_RESOLUTION / rawADC - 1.0);

    Serial.printf("[MQ2 DEBUG] Calculated Rs: %.2f KOhm\n", rs);

    return rs;
}

/**
 * Calibrates the MQ2 sensor by calculating R0 (resistance in clean air)
 * Should be called in clean air environment during setup
 */
void MQ2_calibrate()
{
    Serial.println("\n=== MQ2 CALIBRATION START ===");
    Serial.println("Ensure sensor is in clean air!");
    Serial.println("Warming up sensor (10 seconds)...");
    M5.Lcd.println("MQ2 Warmup...");

    // Wait for sensor to warm up
    delay(10000);

    Serial.printf("Taking %d samples...\n", CALIBRATION_SAMPLES);
    M5.Lcd.println("MQ2 Calibrating...");

    float rsSum = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++)
    {
        float rs = MQ2_readRs();
        rsSum += rs;
        delay(100);

        // Progress indicator
        if ((i + 1) % 10 == 0)
        {
            Serial.printf("  Sample %d/%d: Rs = %.2f K\n", i + 1, CALIBRATION_SAMPLES, rs);
        }
    }

    float avgRs = rsSum / CALIBRATION_SAMPLES;
    MQ2_R0 = avgRs / RO_CLEAN_AIR_FACTOR;

    Serial.printf("Calibration complete!\n");
    Serial.printf("  Average Rs = %.2f K\n", avgRs);
    Serial.printf("  Calculated R0 = %.2f K\n", MQ2_R0);
    Serial.println("=== MQ2 CALIBRATION END ===\n");

    M5.Lcd.println("MQ2 OK");
    MQ2_calibrated = true;
}

/**
 * Reads MQ2 sensor and calculates Rs, Rs/R0 ratio, and smoke PPM
 */
void MQ2_read(SensorData &data)
{
    // Read sensor resistance
    data.mq2_rs = MQ2_readRs();

    // Calculate Rs/R0 ratio
    if (MQ2_R0 > 0)
    {
        data.mq2_ratio = data.mq2_rs / MQ2_R0;
    }
    else
    {
        data.mq2_ratio = -1;  // Invalid
    }

    // Calculate smoke concentration in PPM using empirical formula
    // Smoke curve from MQ2 datasheet: PPM = 605.0 * pow(Rs/R0, -3.5)
    // This is an approximation and may vary with actual conditions
    if (data.mq2_ratio > 0 && data.mq2_ratio < 10)
    {
        data.mq2_smoke_ppm = 605.0 * pow(data.mq2_ratio, -3.5);
    }
    else
    {
        data.mq2_smoke_ppm = 0;  // Out of reliable range
    }
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

    // Initialize and calibrate MQ2 sensor
    Serial.println("Initializing MQ2...");
    M5.Lcd.println("MQ2 Init...");

    // Configure ADC for MQ2 sensor
    analogReadResolution(12);  // Set 12-bit resolution (0-4095)
    analogSetAttenuation(ADC_11db);  // Set attenuation for 0-3.3V range

    pinMode(MQ2_PIN, INPUT);
    MQ2_calibrate();

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

    // Read MQ2 sensor
    MQ2_read(currentReading);

    currentReading.timestamp = currentTime;

    Serial.printf("Temp: %.1fC\n", currentReading.temperature);
    Serial.printf("Humidity: %.1f%%\n", currentReading.humidity);
    Serial.printf("Pressure: %.2f kPa\n", currentReading.pressure);
    Serial.printf("Gas: %.2f KOhm\n", currentReading.gasResistance);
    Serial.printf("MQ2 Rs: %.2f KOhm\n", currentReading.mq2_rs);
    Serial.printf("MQ2 Rs/R0: %.2f\n", currentReading.mq2_ratio);
    Serial.printf("MQ2 Smoke: %.1f PPM\n", currentReading.mq2_smoke_ppm);

    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("T:%.1fC H:%.0f%%\n", currentReading.temperature, currentReading.humidity);
    M5.Lcd.printf("P:%.1f G:%.1f\n", currentReading.pressure, currentReading.gasResistance);
    M5.Lcd.printf("MQ2:%.2f\n", currentReading.mq2_ratio);
    M5.Lcd.printf("Smoke:%.0fPPM\n", currentReading.mq2_smoke_ppm);
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