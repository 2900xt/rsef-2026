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
const String dev_name = "plant_001";
const String location = "39.042388, -77.550108";
const String plant_id = "plant_001";              // Track which plant is being monitored
const String disease_status = "healthy";          // Status: healthy/diseased/unknown
bool is_registered = false;
unsigned long lastUpdate = 0;
unsigned long lastSample = 0;

// Averaging configuration
const int SAMPLES_TO_AVERAGE = 15;  // Take 15 samples before averaging and sending
int sampleCount = 0;

// Temporal analysis configuration
const int BASELINE_WINDOW = 50;                    // Number of samples for rolling statistics
float recent_ratios[BASELINE_WINDOW];              // Rolling window of Rs/R0 ratios
int baseline_index = 0;                            // Current position in rolling window
bool baseline_initialized = false;                 // Track if window is filled

// Accumulated sensor values for averaging
struct AccumulatedData {
    float temperature;
    float humidity;
    float pressure;
    float gasResistance;
    float mq2_rs;
    float mq2_ratio;
    float mq2_smoke_ppm;
    float mq2_delta;
    float mq2_variance;
    float mq2_baseline;
} accumulated;

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
    float mq2_smoke_ppm;    // Estimated smoke concentration in PPM (not used for VOC analysis)
    
    // Temporal features for VOC analysis
    float mq2_delta;        // Change from previous reading
    float mq2_variance;     // Rolling variance over BASELINE_WINDOW
    float mq2_baseline;     // Minimum Rs/R0 over BASELINE_WINDOW
    
    unsigned long timestamp;
};

SensorData currentReading;
SensorData previousReading;  // Track previous for delta calculation

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

    String postData = "{\"name\":\"" + dev_name + 
                      "\", \"timestamp\":" + String(currentReading.timestamp) +
                      ", \"plant_id\":\"" + plant_id + "\"" +
                      ", \"disease_status\":\"" + disease_status + "\"" +
                      ", \"temperature\":" + String(currentReading.temperature, 2) +
                      ", \"humidity\":" + String(currentReading.humidity, 2) +
                      ", \"pressure\":" + String(currentReading.pressure, 3) +
                      ", \"gasResistance\":" + String(currentReading.gasResistance, 2) +
                      ", \"mq2_rs\":" + String(currentReading.mq2_rs, 2) +
                      ", \"mq2_ratio\":" + String(currentReading.mq2_ratio, 4) +
                      ", \"mq2_r0\":" + String(MQ2_R0, 2) +
                      ", \"mq2_delta\":" + String(currentReading.mq2_delta, 4) +
                      ", \"mq2_variance\":" + String(currentReading.mq2_variance, 6) +
                      ", \"mq2_baseline\":" + String(currentReading.mq2_baseline, 4) + "}";

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
 * Computes temporal features for VOC analysis
 * Calculates delta, variance, and baseline from rolling window
 */
void MQ2_computeFeatures(SensorData &current, SensorData &previous)
{
    // Calculate rate of change from previous reading
    current.mq2_delta = current.mq2_ratio - previous.mq2_ratio;
    
    // Update rolling window
    recent_ratios[baseline_index] = current.mq2_ratio;
    baseline_index = (baseline_index + 1) % BASELINE_WINDOW;
    
    // Mark as initialized after first full window
    if (baseline_index == 0) {
        baseline_initialized = true;
    }
    
    // Compute baseline (minimum) and variance
    float min_ratio = recent_ratios[0];
    float sum = 0;
    int samples_to_use = baseline_initialized ? BASELINE_WINDOW : baseline_index;
    
    if (samples_to_use == 0) samples_to_use = 1;  // Prevent division by zero
    
    for (int i = 0; i < samples_to_use; i++) {
        if (recent_ratios[i] < min_ratio) {
            min_ratio = recent_ratios[i];
        }
        sum += recent_ratios[i];
    }
    
    current.mq2_baseline = min_ratio;
    
    // Compute variance
    float mean = sum / samples_to_use;
    float variance_sum = 0;
    for (int i = 0; i < samples_to_use; i++) {
        float diff = recent_ratios[i] - mean;
        variance_sum += diff * diff;
    }
    current.mq2_variance = variance_sum / samples_to_use;
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
    lastSample = millis();
    
    // Initialize accumulated data
    accumulated.temperature = 0;
    accumulated.humidity = 0;
    accumulated.pressure = 0;
    accumulated.gasResistance = 0;
    accumulated.mq2_rs = 0;
    accumulated.mq2_ratio = 0;
    accumulated.mq2_smoke_ppm = 0;
    accumulated.mq2_delta = 0;
    accumulated.mq2_variance = 0;
    accumulated.mq2_baseline = 0;
    sampleCount = 0;
    
    // Initialize previous reading
    previousReading.mq2_ratio = 0;
    previousReading.mq2_delta = 0;
    previousReading.mq2_variance = 0;
    previousReading.mq2_baseline = 0;
    
    // Initialize rolling window for temporal analysis
    for (int i = 0; i < BASELINE_WINDOW; i++) {
        recent_ratios[i] = 0;
    }
    baseline_index = 0;
    baseline_initialized = false;
}

void loop()
{
    unsigned long currentTime = millis();

    // Take a sample every second
    if (currentTime - lastSample >= 1000)
    {
        Serial.printf("\n--- Sample %d/%d ---\n", sampleCount + 1, SAMPLES_TO_AVERAGE);

        if (!bme.performReading())
        {
            Serial.println("ERROR: BME680 read failed");
        }

        SensorData reading;
        reading.temperature = bme.temperature;
        reading.humidity = bme.humidity;
        reading.pressure = bme.pressure / 1000.0;
        reading.gasResistance = bme.gas_resistance / 1000.0;

        // Read MQ2 sensor
        MQ2_read(reading);
        
        // Compute temporal features for VOC analysis
        MQ2_computeFeatures(reading, previousReading);

        // Accumulate the readings
        accumulated.temperature += reading.temperature;
        accumulated.humidity += reading.humidity;
        accumulated.pressure += reading.pressure;
        accumulated.gasResistance += reading.gasResistance;
        accumulated.mq2_rs += reading.mq2_rs;
        accumulated.mq2_ratio += reading.mq2_ratio;
        accumulated.mq2_smoke_ppm += reading.mq2_smoke_ppm;
        accumulated.mq2_delta += reading.mq2_delta;
        accumulated.mq2_variance += reading.mq2_variance;
        accumulated.mq2_baseline += reading.mq2_baseline;
        
        // Store current reading for next delta calculation
        previousReading = reading;

        sampleCount++;

        Serial.printf("T:%.1fC H:%.1f%% P:%.2fkPa G:%.2fK\n", 
                      reading.temperature, reading.humidity, reading.pressure, reading.gasResistance);
        Serial.printf("MQ2 Rs:%.2fK Ratio:%.4f\n",
                      reading.mq2_rs, reading.mq2_ratio);
        Serial.printf("VOC - Delta:%.4f Var:%.6f Base:%.4f\n",
                      reading.mq2_delta, reading.mq2_variance, reading.mq2_baseline);

        // Update display with current sample
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.printf("Sample %d/%d\n", sampleCount, SAMPLES_TO_AVERAGE);
        M5.Lcd.printf("T:%.1fC H:%.0f%%\n", reading.temperature, reading.humidity);
        M5.Lcd.printf("Rs/R0:%.3f\n", reading.mq2_ratio);
        M5.Lcd.printf("D:%.3f V:%.4f\n", reading.mq2_delta, reading.mq2_variance);

        lastSample = currentTime;

        // Check if we have enough samples to average and send
        if (sampleCount >= SAMPLES_TO_AVERAGE)
        {
            Serial.println("\n=== AVERAGING & SENDING ===");

            // Calculate averages
            currentReading.temperature = accumulated.temperature / sampleCount;
            currentReading.humidity = accumulated.humidity / sampleCount;
            currentReading.pressure = accumulated.pressure / sampleCount;
            currentReading.gasResistance = accumulated.gasResistance / sampleCount;
            currentReading.mq2_rs = accumulated.mq2_rs / sampleCount;
            currentReading.mq2_ratio = accumulated.mq2_ratio / sampleCount;
            currentReading.mq2_smoke_ppm = accumulated.mq2_smoke_ppm / sampleCount;
            currentReading.mq2_delta = accumulated.mq2_delta / sampleCount;
            currentReading.mq2_variance = accumulated.mq2_variance / sampleCount;
            currentReading.mq2_baseline = accumulated.mq2_baseline / sampleCount;
            currentReading.timestamp = currentTime;

            Serial.printf("AVERAGED VALUES (n=%d):\n", sampleCount);
            Serial.printf("Environment: T=%.1fC H=%.1f%% P=%.2fkPa\n", 
                         currentReading.temperature, currentReading.humidity, currentReading.pressure);
            Serial.printf("BME680 Gas: %.2f KOhm\n", currentReading.gasResistance);
            Serial.printf("MQ2 Rs: %.2f KOhm\n", currentReading.mq2_rs);
            Serial.printf("MQ2 Rs/R0: %.4f (R0=%.2f)\n", currentReading.mq2_ratio, MQ2_R0);
            Serial.printf("VOC Features - Delta:%.4f Var:%.6f Base:%.4f\n", 
                         currentReading.mq2_delta, currentReading.mq2_variance, currentReading.mq2_baseline);
            Serial.printf("Plant: %s (%s)\n", plant_id.c_str(), disease_status.c_str());

            // Send to server
            if (!is_registered)
            {
                API_register();
            }
            else
            {
                API_update();
            }

            // Update display with averaged values
            M5.Lcd.fillScreen(TFT_BLACK);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.printf("SENT (n=%d)\n", sampleCount);
            M5.Lcd.printf("%s\n", plant_id.c_str());
            M5.Lcd.printf("Rs/R0:%.3f\n", currentReading.mq2_ratio);
            M5.Lcd.printf("Var:%.4f\n", currentReading.mq2_variance);
            M5.Lcd.printf("T:%.1fC H:%.0f%%\n", currentReading.temperature, currentReading.humidity);

            // Reset accumulator
            accumulated.temperature = 0;
            accumulated.humidity = 0;
            accumulated.pressure = 0;
            accumulated.gasResistance = 0;
            accumulated.mq2_rs = 0;
            accumulated.mq2_ratio = 0;
            accumulated.mq2_smoke_ppm = 0;
            accumulated.mq2_delta = 0;
            accumulated.mq2_variance = 0;
            accumulated.mq2_baseline = 0;
            sampleCount = 0;

            lastUpdate = currentTime;
        }
    }

    delay(50);  // Small delay to prevent tight looping
}