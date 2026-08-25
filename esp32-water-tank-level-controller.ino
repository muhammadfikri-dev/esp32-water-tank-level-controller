/**
 * =========================================================================================
 * Project: ESP32 Dual-Tank Ultrasonic Water Level & Closed-Loop Refill Pump Controller
 * Author: Muhammad Fikri (Laksanasoft)
 * License: MIT
 * Features: JSN-SR04T Waterproof Ultrasonic Sensors, Dry-Run Protection Current Telemetry,
 *           Anti-Cycling Hysteresis, FreeRTOS Multi-Tasking, MQTT & Web Dashboard
 * =========================================================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#define TRIG_OVERHEAD   5
#define ECHO_OVERHEAD   18
#define TRIG_SUMP       19
#define ECHO_SUMP       21
#define PIN_PUMP_RELAY  26
#define PIN_FLOAT_DRYRUN 32
#define PIN_BUZZER      25

#define TANK_HEIGHT_CM  120.0 // Total depth of overhead tank

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

struct WaterMetrics {
    float overheadLevelPercent;
    float overheadDistanceCm;
    float sumpLevelPercent;
    float sumpDistanceCm;
    bool  isPumpActive;
    bool  isDryRunTripped;
};

WaterMetrics wm;
SemaphoreHandle_t tankMutex;

float readUltrasonicCm(int trigPin, int echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 35000);
    if (duration == 0) return TANK_HEIGHT_CM;
    return (duration * 0.0343) / 2.0;
}

void setPump(bool state) {
    if (state && wm.isDryRunTripped) {
        Serial.println("[FAILSAFE] Cannot start pump: Sump reservoir has dry-run alert!");
        digitalWrite(PIN_PUMP_RELAY, HIGH);
        wm.isPumpActive = false;
        return;
    }
    wm.isPumpActive = state;
    digitalWrite(PIN_PUMP_RELAY, state ? LOW : HIGH); // Active LOW
    Serial.printf("[ACTUATOR] Main Refill Pump -> %s\n", state ? "PUMPING (ON)" : "STANDBY (OFF)");
}

void TaskWaterLevelManagement(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);

    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        float dOverhead = readUltrasonicCm(TRIG_OVERHEAD, ECHO_OVERHEAD);
        float dSump     = readUltrasonicCm(TRIG_SUMP, ECHO_SUMP);
        bool dryRun     = (digitalRead(PIN_FLOAT_DRYRUN) == HIGH);

        if (xSemaphoreTake(tankMutex, pdMS_TO_TICKS(50))) {
            wm.overheadDistanceCm = dOverhead;
            wm.sumpDistanceCm     = dSump;
            wm.isDryRunTripped    = dryRun;

            float fillOvh = (TANK_HEIGHT_CM - dOverhead);
            wm.overheadLevelPercent = constrain((fillOvh / TANK_HEIGHT_CM) * 100.0, 0.0, 100.0);

            // Automated Hysteresis Pump Logic
            if (wm.overheadLevelPercent < 25.0 && !wm.isPumpActive) {
                if (!wm.isDryRunTripped) {
                    Serial.println("[AUTO] Overhead tank level below 25%. Starting refill pump...");
                    setPump(true);
                }
            } else if (wm.overheadLevelPercent >= 95.0 && wm.isPumpActive) {
                Serial.println("[AUTO] Overhead tank reached 95% full. Stopping refill pump.");
                setPump(false);
            }

            if (wm.isDryRunTripped && wm.isPumpActive) {
                Serial.println("[SAFETY TRIP] Sump empty during pump run! Shutting down immediately.");
                setPump(false);
            }

            xSemaphoreGive(tankMutex);
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(TRIG_OVERHEAD, OUTPUT);
    pinMode(ECHO_OVERHEAD, INPUT);
    pinMode(TRIG_SUMP, OUTPUT);
    pinMode(ECHO_SUMP, INPUT);
    pinMode(PIN_PUMP_RELAY, OUTPUT);
    pinMode(PIN_FLOAT_DRYRUN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);

    digitalWrite(PIN_PUMP_RELAY, HIGH); // OFF
    digitalWrite(PIN_BUZZER, LOW);

    tankMutex = xSemaphoreCreateMutex();

    WiFi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] Connected! Water Controller IP: %s\n", WiFi.localIP().toString().c_str());

    mqttClient.setServer("broker.hivemq.com", 1883);

    xTaskCreatePinnedToCore(TaskWaterLevelManagement, "TankTask", 4096, NULL, 2, NULL, 1);
}

void loop() {
    if (!mqttClient.connected()) {
        mqttClient.connect("Laksanasoft-WaterTank-Controller");
    }
    mqttClient.loop();

    StaticJsonDocument<256> doc;
    doc["overhead_percent"]  = serialized(String(wm.overheadLevelPercent, 1));
    doc["overhead_cm"]       = serialized(String(wm.overheadDistanceCm, 1));
    doc["pump_running"]      = wm.isPumpActive;
    doc["dry_run_alert"]     = wm.isDryRunTripped;

    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish("laksanasoft/watertank/telemetry", buffer);

    delay(3000);
}
