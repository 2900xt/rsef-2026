#include <config.h>

#include <M5StickCPlus.h>
#include <Wire.h>
#include <vector>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME680 bme;

#include <TroykaMQ.h>
#define MQ2_PIN 36
#define MQ2_RL 10.0
MQ2 mq2(MQ2_PIN);
float mq2_ro = 10.0;
bool mq2_calibrated = false;

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
WiFiMulti wifiMulti;
HTTPClient http;
const char *server_ip = "http://192.168.1.251:5000/";
const String dev_name = "sensor_02";
const String location = "39.042388, -77.550108";

bool is_registered = false;
unsigned long lastUpdate = 0;
unsigned long animationTimer = 0;
int animationFrame = 0;
bool wifiConnected = false;

struct SensorData
{
    float temperature;
    float humidity;
    float pressure;
    float gasResistance;
    float lpg;
    float ch4;
    float smoke;
    unsigned long timestamp;
};

SensorData currentReading;
SensorData previousReading;

void drawSensorValue(int x, int y, const char *label, float value, const char *unit, float prevValue, uint16_t color)
{
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Lcd.setCursor(x, y);
    M5.Lcd.print(label);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(color, TFT_BLACK);
    M5.Lcd.setCursor(x, y + 15);
    M5.Lcd.printf("%.1f", value);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Lcd.print(" ");
    M5.Lcd.print(unit);

    if (abs(value - prevValue) > 0.01)
    {
        M5.Lcd.setTextColor(value > prevValue ? TFT_GREEN : TFT_RED, TFT_BLACK);
        M5.Lcd.print(value > prevValue ? " +" : "-");
    }
}

void API_register()
{
    String callpoint = server_ip;
    callpoint += "register";

    http.begin(callpoint);
    http.addHeader("Content-Type", "application/json");

    String postData = "{\"name\":\"" + dev_name + "\", \"location\":\"" + location + "\"}";

    int httpCode = http.POST(postData);
    if (httpCode == 200)
    {
        is_registered = true;
    }
    else 
    {
        // write API error to LCD
        M5.Lcd.setCursor(150, 10);
        M5.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Lcd.printf("API REG ERR %d", httpCode);
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
                      ", \"lpg\":" + String(currentReading.lpg) +
                      ", \"ch4\":" + String(currentReading.ch4) +
                      ", \"smoke\":" + String(currentReading.smoke) +
                      ", \"timestamp\":" + String(currentReading.timestamp) + "}";

    int httpCode = http.POST(postData);
    if (httpCode != 200)
    {
        // write API error to LCD
        M5.Lcd.setCursor(150, 10);
        M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
        M5.Lcd.printf("API ERR %d", httpCode);
        fail_count++;
        if (fail_count > 5)
        {
            is_registered = false;
            fail_count = 0;
        }
    }

    http.end();
}

void setup()
{
    M5.begin();
    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(TFT_BLACK);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Lcd.setCursor(20, 50);
    M5.Lcd.println("CROPSENSE");

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(30, 80);
    M5.Lcd.println("Initializing...");


    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWD);

    M5.Lcd.setCursor(30, 100);
    M5.Lcd.println("Connecting WiFi...");

    if (wifiMulti.run() == WL_CONNECTED)
    {
        wifiConnected = true;
        M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Lcd.setCursor(30, 120);
        M5.Lcd.println("WiFi Connected!");
    }

    Wire.begin(32, 33);

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(30, 140);
    M5.Lcd.println("Init BME680...");

    if (!bme.begin(0x77))
    {
        M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
        M5.Lcd.setCursor(30, 160);
        M5.Lcd.println("BME680 ERROR!");
        while (1)
            ;
    }

    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);

    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Lcd.setCursor(30, 160);
    M5.Lcd.println("BME680 Ready!");

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(30, 180);
    M5.Lcd.println("Calibrating MQ2...");

    mq2.calibrate();
    mq2_ro = mq2.getRo();
    mq2_calibrated = true;

    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Lcd.setCursor(30, 200);
    M5.Lcd.printf("MQ2 Ro: %.2f", mq2_ro);

    delay(2000);
    M5.Lcd.fillScreen(TFT_BLACK);

    lastUpdate = millis();
    animationTimer = millis();

    delay(1000);
}

void loop()
{
    unsigned long currentTime = millis();

    if (currentTime - animationTimer > 250)
    {
        animationFrame = (animationFrame + 1) % 4;
        animationTimer = currentTime;
    }

    if (currentTime - lastUpdate < 1000)
    {
        delay(100);
        return;
    }

    if (!bme.performReading())
    {
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
        M5.Lcd.setCursor(30, 100);
        M5.Lcd.println("SENSOR ERROR");
        delay(1000);
        return;
    }

    previousReading = currentReading;

    currentReading.temperature = bme.temperature;
    currentReading.humidity = bme.humidity;
    currentReading.pressure = bme.pressure / 1000.0;
    currentReading.gasResistance = bme.gas_resistance / 1000.0;
    
    if (mq2_calibrated) {
        currentReading.lpg = mq2.readLPG();
        currentReading.ch4 = mq2.readMethane();
        currentReading.smoke = mq2.readSmoke();
    } else {
        currentReading.lpg = 0;
        currentReading.ch4 = 0;
        currentReading.smoke = 0;
    }
    
    currentReading.timestamp = currentTime;

    M5.Lcd.fillScreen(TFT_BLACK);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("CROPSENSE");

    drawSensorValue(10, 40, "TEMP", currentReading.temperature, "C",
                    previousReading.temperature, TFT_RED);

    drawSensorValue(130, 40, "HUMIDITY", currentReading.humidity, "%",
                    previousReading.humidity, TFT_BLUE);

    drawSensorValue(10, 100, "PRESSURE", currentReading.pressure, "kPa",
                    previousReading.pressure, TFT_GREEN);

    drawSensorValue(130, 100, "GAS", currentReading.gasResistance, "K",
                    previousReading.gasResistance, TFT_YELLOW);

    lastUpdate = currentTime;

    if (wifiConnected && !is_registered)
    {
        API_register();
    }
    else 
    {
        API_update();
    }
    
    delay(100);
}