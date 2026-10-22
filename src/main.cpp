#include <Arduino.h>
#include <cstring>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_system.h>

#if __has_include("mqtt_secrets.h")
#include "mqtt_secrets.h"
#else
#include "mqtt_secrets.example.h"
#endif

// ======================================================
// NHOM 14 - DE TAI 31
// Bang chuyen dem va phan loai san pham theo chieu cao
// Wokwi + PlatformIO + MQTT + Node-RED
// ======================================================

// ---------------------- GPIO ---------------------------
#define IR_IN_PIN 27
#define IR_OUT_PIN 26
#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_PIN 13
#define MOTOR_IN1_PIN 32 // L298N IN1
#define MOTOR_IN2_PIN 33 // L298N IN2
#define MOTOR_ENA_PIN 25 // L298N ENA (on/off enable; remove ENA jumper)

// ------------------- Cau hinh do -----------------------
const float SENSOR_TO_BELT_CM = 20.0F;
float minHeightCm = 8.0F;
float maxHeightCm = 12.0F;

const uint8_t DISTANCE_SAMPLE_COUNT = 5;
const uint8_t MIN_VALID_SAMPLE_COUNT = 3;
const unsigned long ECHO_TIMEOUT_US = 30000UL;

// ---------------- Wi-Fi va MQTT ------------------------
const char *TOPIC_TELEMETRY =
    "huit/nhom14/detai31/conveyor/telemetry";
const char *TOPIC_EVENT =
    "huit/nhom14/detai31/conveyor/event";
const char *TOPIC_STATUS =
    "huit/nhom14/detai31/conveyor/status";
const char *TOPIC_KEEPALIVE =
    "huit/nhom14/detai31/conveyor/keepalive";
const char *TOPIC_COMMAND =
    "huit/nhom14/detai31/conveyor/command";

const unsigned long WIFI_RETRY_MS = 10000UL;
const unsigned long MQTT_RETRY_MS = 5000UL;
const unsigned long HEARTBEAT_MS = 5000UL;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Servo servoGate;

// ---------------- Trang thai he thong ------------------
int totalCount = 0;
int passCount = 0;
int rejectCount = 0;
int measureErrorCount = 0;

bool systemEnabled = false; // Require an explicit START command after boot.
bool waitingForExit = false;
bool exitSensorWasBlocked = false;

float lastDistanceCm = -1.0F;
float lastHeightCm = -1.0F;
String lastResult = "NONE";
String lastReason = "BOOT";

unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastHeartbeatMs = 0;
uint32_t bootSessionId = 0;
uint32_t measurementSequence = 0;

// ======================================================
// TIEN ICH
// ======================================================

float rejectRate() {
  if (totalCount == 0) {
    return 0.0F;
  }
  return (rejectCount * 100.0F) / totalCount;
}

void sortFloatArray(float *values, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    float current = values[i];
    int8_t j = i - 1;

    while (j >= 0 && values[j] > current) {
      values[j + 1] = values[j];
      j--;
    }

    values[j + 1] = current;
  }
}

// ======================================================
// DOC HC-SR04
// ======================================================

float readDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  const unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);

  if (duration == 0) {
    return -1.0F;
  }

  const float distanceCm = duration / 58.0F;

  // Loai bo gia tri ngoai mien hop ly cua bai demo.
  if (distanceCm < 2.0F || distanceCm > 400.0F) {
    return -1.0F;
  }

  return distanceCm;
}

float readDistanceMedian() {
  float validSamples[DISTANCE_SAMPLE_COUNT];
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < DISTANCE_SAMPLE_COUNT; i++) {
    const float sample = readDistanceOnce();

    if (sample > 0.0F) {
      validSamples[validCount] = sample;
      validCount++;
    }

    delay(35);
  }

  if (validCount < MIN_VALID_SAMPLE_COUNT) {
    return -1.0F;
  }

  sortFloatArray(validSamples, validCount);

  if (validCount % 2 == 1) {
    return validSamples[validCount / 2];
  }

  return (validSamples[(validCount / 2) - 1] +
          validSamples[validCount / 2]) /
         2.0F;
}

// ======================================================
// MQTT
// ======================================================

void publishStatus(const char *state, const char *message) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[384];
  snprintf(payload, sizeof(payload),
           "{\"device\":\"esp32-conveyor-nhom14\","
           "\"state\":\"%s\",\"message\":\"%s\","
           "\"enabled\":%s,\"total\":%d,\"pass\":%d,"
           "\"reject\":%d,\"measure_error\":%d,"
           "\"reject_rate\":%.1f,\"uptime_ms\":%lu}",
           state, message, systemEnabled ? "true" : "false", totalCount,
           passCount, rejectCount, measureErrorCount, rejectRate(), millis());

  mqttClient.publish(TOPIC_STATUS, payload, true);
}

void publishTelemetry() {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[640];
  snprintf(payload, sizeof(payload),
           "{\"device\":\"esp32-conveyor-nhom14\","
           "\"distance_cm\":%.2f,\"height_cm\":%.2f,"
           "\"result\":\"%s\",\"reason\":\"%s\","
           "\"sample_id\":\"%08lX-%lu\","
           "\"total\":%d,\"pass\":%d,\"reject\":%d,"
           "\"measure_error\":%d,\"reject_rate\":%.1f,"
           "\"enabled\":%s,\"wifi_rssi\":%d,"
           "\"uptime_ms\":%lu}",
           lastDistanceCm, lastHeightCm, lastResult.c_str(),
           lastReason.c_str(), static_cast<unsigned long>(bootSessionId),
           static_cast<unsigned long>(measurementSequence), totalCount,
           passCount, rejectCount,
           measureErrorCount, rejectRate(),
           systemEnabled ? "true" : "false",
           static_cast<int>(WiFi.RSSI()), millis());

  mqttClient.publish(TOPIC_TELEMETRY, payload, false);
}

void publishEvent(const char *eventName) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[320];
  snprintf(payload, sizeof(payload),
           "{\"event\":\"%s\",\"result\":\"%s\","
           "\"reason\":\"%s\",\"height_cm\":%.2f,"
           "\"total\":%d,\"pass\":%d,\"reject\":%d,"
           "\"uptime_ms\":%lu}",
           eventName, lastResult.c_str(), lastReason.c_str(), lastHeightCm,
           totalCount, passCount, rejectCount, millis());

  mqttClient.publish(TOPIC_EVENT, payload, false);
}

// Heartbeat ung dung, doc lap voi MQTT protocol Keep Alive.
void publishHeartbeat() {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"device\":\"esp32-conveyor-nhom14\","
           "\"online\":true,\"uptime_ms\":%lu}",
           millis());
  // Do not retain a heartbeat: a retained ONLINE packet could look fresh
  // after Node-RED restarts while the ESP32 is already offline.
  mqttClient.publish(TOPIC_KEEPALIVE, payload, false);
}

void resetCounters() {
  totalCount = 0;
  passCount = 0;
  rejectCount = 0;
  measureErrorCount = 0;
  lastDistanceCm = -1.0F;
  lastHeightCm = -1.0F;
  lastResult = "NONE";
  lastReason = "RESET";
}

void setConveyorRunning(bool running) {
  if (running) {
    digitalWrite(MOTOR_IN1_PIN, HIGH);
    digitalWrite(MOTOR_IN2_PIN, LOW);
    digitalWrite(MOTOR_ENA_PIN, HIGH);
  } else {
    digitalWrite(MOTOR_ENA_PIN, LOW);
    digitalWrite(MOTOR_IN1_PIN, LOW);
    digitalWrite(MOTOR_IN2_PIN, LOW);
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, TOPIC_COMMAND) != 0) {
    return;
  }
  String command;

  for (unsigned int i = 0; i < length; i++) {
    command += static_cast<char>(payload[i]);
  }

  command.trim();
  command.toUpperCase();

  Serial2.print("MQTT command: ");
  Serial2.println(command);

  // Chap nhan ca chuoi thuan START va JSON co chua "START".
  if (command.indexOf("START") >= 0) {
    systemEnabled = true;
    setConveyorRunning(true);
    publishStatus("RUN", "System started from Node-RED");
  } else if (command.indexOf("STOP") >= 0) {
    systemEnabled = false;
    setConveyorRunning(false);
    servoGate.write(0);
    publishStatus("STOP", "System stopped from Node-RED");
  } else if (command.indexOf("RESET") >= 0) {
    resetCounters();
    publishStatus(systemEnabled ? "RUN" : "STOP",
                  "Counters reset from Node-RED");
    publishTelemetry();
  } else {
    publishStatus("WARNING", "Unknown command");
  }
}

void connectWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_MS) {
    return;
  }

  lastWifiAttemptMs = now;
  Serial2.println("Dang ket noi Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_MS) {
    return;
  }

  lastMqttAttemptMs = now;

  String clientId = "nhom14-esp32-";
  clientId += String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);

  Serial2.print("Dang ket noi MQTT voi client id: ");
  Serial2.println(clientId);

  bool connected = false;
  if (MQTT_USERNAME[0] != '\0') {
    connected = mqttClient.connect(
        clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, TOPIC_STATUS, 0, true,
        "{\"state\":\"OFFLINE\"}");
  } else {
    connected = mqttClient.connect(clientId.c_str(), TOPIC_STATUS, 0, true,
                                   "{\"state\":\"OFFLINE\"}");
  }

  if (connected) {
    mqttClient.subscribe(TOPIC_COMMAND);
    publishStatus(systemEnabled ? "RUN" : "STOP", "MQTT connected");
    publishHeartbeat();
    lastHeartbeatMs = millis();
    Serial2.println("MQTT connected.");
  } else {
    Serial2.print("MQTT failed, state = ");
    Serial2.println(mqttClient.state());
  }
}

// ======================================================
// CO CAU GAT VA THONG KE
// ======================================================

void rejectProduct() {
  Serial2.println(">> Servo dang gat san pham loi...");
  servoGate.write(90);
  delay(800);
  servoGate.write(0);
  delay(500);
  Serial2.println(">> Servo da ve vi tri ban dau.");
}

void printStatistics() {
  Serial2.println();
  Serial2.println("========== THONG KE ==========");
  Serial2.print("TOTAL         : ");
  Serial2.println(totalCount);
  Serial2.print("PASS          : ");
  Serial2.println(passCount);
  Serial2.print("REJECT        : ");
  Serial2.println(rejectCount);
  Serial2.print("MEASURE ERROR : ");
  Serial2.println(measureErrorCount);
  Serial2.print("TY LE LOI     : ");
  Serial2.print(rejectRate(), 1);
  Serial2.println("%");
  Serial2.println("==============================");
}

// ======================================================
// XU LY MOT SAN PHAM
// ======================================================

void reportMeasurementError(const char *reason) {
  measureErrorCount++;
  lastDistanceCm = -1.0F;
  lastHeightCm = -1.0F;
  lastResult = "MEASURE_ERROR";
  lastReason = reason;

  Serial2.print("LOI DO: ");
  Serial2.println(reason);
  Serial2.println("Khong cong vao Total/Pass/Reject.");

  publishEvent("MEASURE_ERROR");
  publishTelemetry();
  publishStatus(systemEnabled ? "RUN" : "STOP", reason);
  printStatistics();
}

void processProduct() {
  Serial2.println();
  Serial2.println("====================================");
  Serial2.println("PHAT HIEN SAN PHAM MOI");
  Serial2.println("====================================");

  const float distanceCm = readDistanceMedian();

  if (distanceCm < 0.0F) {
    reportMeasurementError("NO_VALID_ECHO");
    return;
  }

  // HC-SR04 beyond the calibrated 20 cm belt plane means there is no product
  // in the measurement zone. Do not create a negative height or count it as
  // PASS/REJECT/MEASURE_ERROR.
  if (distanceCm > SENSOR_TO_BELT_CM) {
    lastDistanceCm = distanceCm;
    lastHeightCm = -1.0F;
    lastResult = "NONE";
    lastReason = "NO_PRODUCT";
    Serial2.println("Khong co san pham trong vung do (d > H = 20 cm).");
    Serial2.println("Bo qua, khong cong vao Total/Pass/Reject.");
    publishTelemetry();
    return;
  }

  const float heightCm = SENSOR_TO_BELT_CM - distanceCm;

  if (heightCm < 0.0F || heightCm > SENSOR_TO_BELT_CM) {
    reportMeasurementError("HEIGHT_OUT_OF_RANGE");
    return;
  }

  lastDistanceCm = distanceCm;
  lastHeightCm = heightCm;

  Serial2.print("Khoang cach median d : ");
  Serial2.print(distanceCm, 2);
  Serial2.println(" cm");
  Serial2.print("Chieu cao h          : ");
  Serial2.print(heightCm, 2);
  Serial2.println(" cm");

  measurementSequence++;
  totalCount++;

  if (heightCm >= minHeightCm && heightCm <= maxHeightCm) {
    passCount++;
    lastResult = "PASS";
    lastReason = "IN_RANGE";
    Serial2.println("KET QUA: PASS");
  } else {
    rejectCount++;
    lastResult = "REJECT";

    if (heightCm < minHeightCm) {
      lastReason = "TOO_LOW";
      Serial2.println("KET QUA: REJECT - SAN PHAM QUA THAP");
    } else {
      lastReason = "TOO_HIGH";
      Serial2.println("KET QUA: REJECT - SAN PHAM QUA CAO");
    }

    // Quyet dinh va gạt tai ESP32, khong cho round-trip Node-RED.
    rejectProduct();
  }

  publishEvent("PRODUCT_CLASSIFIED");
  publishTelemetry();
  publishStatus(systemEnabled ? "RUN" : "STOP", "Product classified");
  printStatistics();
}

// ======================================================
// SETUP VA LOOP
// ======================================================

void setup() {
  bootSessionId = esp_random();

  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  delay(500);

  // Wokwi pushbuttons emulate active-low IR beam sensors.
  pinMode(IR_IN_PIN, INPUT_PULLUP);
  pinMode(IR_OUT_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  pinMode(MOTOR_ENA_PIN, OUTPUT);
  setConveyorRunning(systemEnabled);

  servoGate.attach(SERVO_PIN);
  servoGate.write(0);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(768);
  mqttClient.setKeepAlive(15); // MQTT protocol Keep Alive, seconds

  Serial2.println();
  Serial2.println("======================================");
  Serial2.println(" NHOM 14 - BANG CHUYEN PHAN LOAI IoT");
  Serial2.println("======================================");
  Serial2.print("H = ");
  Serial2.print(SENSOR_TO_BELT_CM);
  Serial2.println(" cm");
  Serial2.print("Khoang PASS = ");
  Serial2.print(minHeightCm);
  Serial2.print(" - ");
  Serial2.print(maxHeightCm);
  Serial2.println(" cm");
  Serial2.println("HE THONG SAN SANG.");
}

void loop() {
  connectWiFiIfNeeded();
  connectMqttIfNeeded();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  const int irInState = digitalRead(IR_IN_PIN);
  const int irOutState = digitalRead(IR_OUT_PIN);

  if (systemEnabled && irInState == LOW && !waitingForExit) {
    // IR_IN bat dau chu ky; IR_OUT clear moi cho phep nhan san pham tiep theo.
    waitingForExit = true;
    exitSensorWasBlocked = false;
    processProduct();
  }

  if (waitingForExit && irOutState == LOW && !exitSensorWasBlocked) {
    exitSensorWasBlocked = true;
    Serial2.println("IR_OUT blocked: da phat hien san pham tai dau ra.");
    publishStatus(systemEnabled ? "RUN" : "STOP",
                  "Product detected at IR_OUT");
  }

  if (waitingForExit && exitSensorWasBlocked && irOutState == HIGH) {
    waitingForExit = false;
    exitSensorWasBlocked = false;
    Serial2.println("IR_OUT clear: san pham da roi, re-arm.");
    publishStatus(systemEnabled ? "RUN" : "STOP",
                  "Ready for next product");
  }

  const unsigned long now = millis();
  if (now - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    publishHeartbeat();
  }

  delay(20);
}
