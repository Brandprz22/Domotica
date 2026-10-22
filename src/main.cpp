#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

const char *ssid = "Rick_Lab_AP";
const char *password = "morty1234";

const int LED1_PIN = 16;
const int LED2_PIN = 17;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void broadcastState() {
  StaticJsonDocument<128> doc;
  JsonObject lights = doc.createNestedObject("lights");
  lights["led1"] = digitalRead(LED1_PIN) ? 1 : 0;
  lights["led2"] = digitalRead(LED2_PIN) ? 1 : 0;
  String payload;
  serializeJson(doc, payload);
  ws.textAll(payload);
}

bool toggleTarget(const char *target) {
  if (target == nullptr) return false;
  if (strcmp(target, "led1") == 0) {
    digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
    return true;
  }
  if (strcmp(target, "led2") == 0) {
    digitalWrite(LED2_PIN, !digitalRead(LED2_PIN));
    return true;
  }
  return false;
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

  data[len] = '\0';

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, (const char *)data);
  if (err) {
    char *token = strtok((char *)data, " ");
    if (token != nullptr && strcmp(token, "toggle") == 0) {
      char *target = strtok(nullptr, " ");
      if (toggleTarget(target)) broadcastState();
    }
    return;
  }

  const char *target = nullptr;
  if (doc["toggle"].is<const char *>()) target = doc["toggle"].as<const char *>();
  else if (doc["target"].is<const char *>()) target = doc["target"].as<const char *>();
  else if (doc["zone"].is<const char *>()) target = doc["zone"].as<const char *>();
  else if (doc["led"].is<const char *>()) target = doc["led"].as<const char *>();
  else if (doc["name"].is<const char *>()) target = doc["name"].as<const char *>();

  const char *action = doc["action"].is<const char *>() ? doc["action"].as<const char *>() : nullptr;
  if (action != nullptr && strcmp(action, "toggle") != 0) return;

  if (toggleTarget(target)) broadcastState();
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
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  if (!LittleFS.begin(true)) {
    Serial.println("Error montando LittleFS");
    return;
  }

  WiFi.softAP(ssid, password);

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.begin();
  Serial.println("Servidor y WebSockets listos.");
}

void loop() {
  ws.cleanupClients();
}
