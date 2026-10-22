#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

const char *ssid = "Rick_Lab_AP";
const char *password = "morty1234";

const int LED1_PIN = 17;
const int LED2_PIN = 16;
const int SERVO_PIN = 13;
const int FAN_PWM_PIN = 18;
const int FAN_DIR_PIN = 19;
const int LM35_PIN = 34;
const int LDR_PIN = 35;

const int FAN_CHANNEL = 2;
const int LDR_THRESHOLD = 800;
const unsigned long TELEMETRY_INTERVAL = 1000;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Servo servo;

float tempC = 0.0;
int ldrValue = 0;
int fanSpeed = 0;
int servoAngle = 90;
unsigned long lastTelemetry = 0;

void broadcastState() {
  StaticJsonDocument<256> doc;
  doc["temp"] = tempC;
  doc["ldr"] = ldrValue;
  doc["led1"] = digitalRead(LED1_PIN) ? 1 : 0;
  doc["led2"] = digitalRead(LED2_PIN) ? 1 : 0;
  doc["fan"] = fanSpeed;
  doc["servo"] = servoAngle;

  String payload;
  serializeJson(doc, payload);
  ws.textAll(payload);
}

void readSensorsAndUpdateLed2() {
  uint32_t millivoltSum = 0;
  for (int i = 0; i < 10; i++) {
    millivoltSum += analogReadMilliVolts(LM35_PIN);
    delay(2);
  }

  float averageMillivolts = millivoltSum / 10.0f;
  tempC = averageMillivolts / 10.0f;

  ldrValue = analogRead(LDR_PIN);
  digitalWrite(LED2_PIN, ldrValue < LDR_THRESHOLD ? HIGH : LOW);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

  data[len] = '\0';

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, (const char *)data)) return;

  const char *action = doc["action"].is<const char *>() ? doc["action"].as<const char *>() : "";
  bool handled = false;

  if (strcmp(action, "toggle") == 0) {
    const char *target = doc["target"].is<const char *>() ? doc["target"].as<const char *>() : "";
    if (strcmp(target, "led1") == 0) {
      digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
      handled = true;
    }
  } else if (strcmp(action, "fan") == 0) {
    int speed = doc["speed"] | 0;
    speed = constrain(speed, 0, 100);
    fanSpeed = speed;

    uint32_t dutyCycle = 0;
    if (speed > 0) {
      dutyCycle = map(speed, 1, 100, 75, 255);
    }
    ledcWrite(FAN_CHANNEL, dutyCycle);
    handled = true;
  } else if (strcmp(action, "servo") == 0) {
    int angle = doc["angle"] | 90;
    angle = constrain(angle, 0, 180);
    servoAngle = angle;
    servo.write(angle);
    handled = true;
  }

  if (handled) broadcastState();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    broadcastState();
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(FAN_PWM_PIN, OUTPUT);
  pinMode(FAN_DIR_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(FAN_DIR_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(LM35_PIN, ADC_11db);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  ledcSetup(FAN_CHANNEL, 1000, 8);
  ledcAttachPin(FAN_PWM_PIN, FAN_CHANNEL);
  ledcWrite(FAN_CHANNEL, 0);

  ESP32PWM::allocateTimer(0);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(servoAngle);

  if (!LittleFS.begin(true)) {
    Serial.println("Error montando LittleFS");
    return;
  }

  WiFi.softAP(ssid, password);

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  readSensorsAndUpdateLed2();
  lastTelemetry = millis();

  server.begin();
  Serial.println("Servidor y WebSockets listos.");
}

void loop() {
  ws.cleanupClients();

  unsigned long now = millis();
  if (now - lastTelemetry >= TELEMETRY_INTERVAL) {
    lastTelemetry = now;
    readSensorsAndUpdateLed2();
    broadcastState();
  }
}
