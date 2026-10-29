// ============================================================================
//  Maqueta domótica C-137 · Firmware ESP32 (DOIT DevKit V1)
//  LittleFS + ESPAsyncWebServer + AsyncWebSocket. Bucle totalmente no bloqueante.
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <math.h>

// ------------------------------- Red local ----------------------------------
const char *AP_SSID = "Rick_Lab_AP";
const char *AP_PASS = "morty1234";

// --------------------------- Asignación de pines ----------------------------
// LM35, LDR y MQ-2 viven en ADC1 para convivir con el Wi-Fi.
const uint8_t PIN_LM35 = 34;   // D34 · 10 mV/°C
const uint8_t PIN_LDR = 35;    // D35 · divisor 10 kΩ a 3.3 V
const uint8_t PIN_MQ2 = 32;    // D32 · divisor 10 kΩ / 20 kΩ
const uint8_t PIN_KY038 = 33;  // D33 · salida digital D0 (LOW = sonido)
const uint8_t PIN_SERVO = 13;  // D13 · SG90 (parasol)
const uint8_t PIN_FAN_PWM = 18; // D18 · L9110S PWM
const uint8_t PIN_FAN_DIR = 19; // D19 · L9110S dirección (siempre LOW)
const uint8_t PIN_LED1 = 17;   // D17 · LED manual
const uint8_t PIN_LED2 = 16;   // D16 · LED automático por LDR

// ------------------------------- Constantes ---------------------------------
const uint32_t TELEMETRY_MS = 1000;     // Emisión JSON
const uint32_t SENSOR_MS = 50;          // Lectura de ADC
const uint32_t SERVO_STEP_MS = 20;      // Barrido del SG90
const uint32_t SOUND_WINDOW_MS = 100;   // Ventana de conteo de pulsos del D0
const uint32_t GAS_WARMUP_MS = 20000;   // Precalentamiento del MQ-2

const int LDR_LED2_THRESHOLD = 800;     // LED 2: oscuridad
const int LDR_SUN_OPEN = 2500;          // Parasol: sol directo
const int LDR_SUN_CLOSE = 1200;         // Parasol: sombra/noche
const int GAS_ALERT_ON = 1500;          // MQ-2: disparo
const int GAS_ALERT_OFF = 1300;         // MQ-2: despeje
const int SOUND_ACTIVE_PULSES = 5;      // Pulsos por ventana que cuentan como llanto
const int SOUND_LEVEL_STEP = 10;        // Escala 0–100 para la web
const int SOUND_LEVEL_MAX = 100;
const uint32_t BABY_TRIGGER_MS = 3000;  // Racha equivalente de llanto
const uint32_t BABY_DECAY_MS = 50;      // Se resta por cada ventana silenciosa
const uint32_t BABY_REARM_MS = 2000;    // Silencio continuo para rearmar
const uint8_t SERVO_STEP_DEG = 5;       // 5° cada 20 ms ≈ 720 ms de recorrido

const int FAN_CHANNEL = 2;              // LEDC canal 2 -> timer 1
const int FAN_FREQ = 1000;
const int FAN_RESOLUTION = 8;
const int FAN_MIN_DUTY = 75;            // Arranque asistido del motor 130
const int FAN_MAX_DUTY = 255;
const int FAN_AUTO_UI = 50;             // Arranque automático al 50 %
const float TEMP_SETPOINT_MIN = 10.0f;
const float TEMP_SETPOINT_MAX = 40.0f;
const float TEMP_SETPOINT_DEFAULT = 26.0f;
const float TEMP_HYSTERESIS = 1.0f;
const float LM35_MV_PER_DEGREE = 10.0f;

// -------------------------------- Estado ------------------------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Servo parasolServo;

float tempC = 0.0f;
float tempSetpoint = TEMP_SETPOINT_DEFAULT;
int ldrRaw = 0;
int gasRaw = 0;
int soundLevel = 0;          // 0–100 para la web
int fanUI = 0;
bool led1On = false;
bool led2On = false;
bool gasAlert = false;
bool soundLoud = false;      // La última ventana superó el umbral de pulsos
bool babyAlert = false;
bool babyArmed = true;       // ¿Se permite un nuevo disparo?
bool babyQuietTracking = false;
bool fanManual = false;
bool parasolManual = false;
uint8_t parasolAngle = 0;   // 0 = cerrado, 180 = abierto
uint8_t parasolTarget = 0;

uint32_t lastSensorRead = 0;
uint32_t lastServoStep = 0;
uint32_t lastTelemetry = 0;
uint32_t soundWindowStart = 0;
uint32_t babyQuietSince = 0;
uint32_t cryingMs = 0;       // Integrador con fuga de la racha de llanto
uint16_t soundPulses = 0;    // Flancos de bajada dentro de la ventana
bool lastSoundLow = false;   // Nivel previo del D0 para detectar el flanco
bool firstTempRead = true;

// ------------------------------ Utilidades ----------------------------------
int averageAnalog(uint8_t pin, uint8_t samples) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(pin);
  }
  return (int)(sum / samples);
}

// 0 % apaga el motor; 1–100 % se mapea a 75–255 para vencer la inercia.
void applyFan(int percent) {
  percent = constrain(percent, 0, 100);
  fanUI = percent;
  uint32_t duty = 0;
  if (percent > 0) {
    duty = (uint32_t)map(percent, 1, 100, FAN_MIN_DUTY, FAN_MAX_DUTY);
  }
  ledcWrite(FAN_CHANNEL, duty);
}

void updateFanAutomation() {
  if (tempC > tempSetpoint && fanUI == 0 && !fanManual) {
    applyFan(FAN_AUTO_UI);
  }
  if (tempC < (tempSetpoint - TEMP_HYSTERESIS)) {
    fanManual = false;          // La histéresis devuelve el control a la automática
    if (fanUI != 0) applyFan(0);
  }
}

void updateParasolAutomation() {
  const bool inNeutralBand = (ldrRaw >= LDR_SUN_CLOSE && ldrRaw <= LDR_SUN_OPEN);

  if (parasolManual) {
    if (inNeutralBand) parasolManual = false;  // El override se libera solo
    return;
  }

  if (ldrRaw > LDR_SUN_OPEN) {
    parasolTarget = 180;
  } else if (ldrRaw < LDR_SUN_CLOSE) {
    parasolTarget = 0;
  }
}

void updateServoSweep(uint32_t now) {
  if (now - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = now;
  if (parasolAngle == parasolTarget) return;

  if (parasolAngle < parasolTarget) {
    parasolAngle = min<int>(parasolTarget, parasolAngle + SERVO_STEP_DEG);
  } else {
    parasolAngle = max<int>(parasolTarget, (int)parasolAngle - SERVO_STEP_DEG);
  }
  parasolServo.write(parasolAngle);
}

void updateBabyLogic(uint32_t now, bool loud) {
  // Alerta enclavada: solo el comando clear_baby_alert la retira.
  if (babyAlert) return;

  // Puerta de rearme: 2 s continuos por debajo del umbral tras descartar.
  if (!babyArmed) {
    cryingMs = 0;
    if (loud) {
      babyQuietTracking = false;
      return;
    }
    if (!babyQuietTracking) {
      babyQuietTracking = true;
      babyQuietSince = now;
    } else if (now - babyQuietSince >= BABY_REARM_MS) {
      babyArmed = true;
      babyQuietTracking = false;
    }
    return;
  }

  // Integrador con fuga: +100 ms por ventana activa, −50 ms por ventana en calma.
  if (loud) {
    if (cryingMs < BABY_TRIGGER_MS) cryingMs += SOUND_WINDOW_MS;
    if (cryingMs >= BABY_TRIGGER_MS) {
      babyAlert = true;
      babyArmed = false;
      babyQuietTracking = false;
      cryingMs = 0;
    }
  } else if (cryingMs > BABY_DECAY_MS) {
    cryingMs -= BABY_DECAY_MS;
  } else {
    cryingMs = 0;
  }
}

void readSensors(uint32_t now) {
  if (now - lastSensorRead < SENSOR_MS) return;
  lastSensorRead = now;

  // LM35 con filtro exponencial: estable sin bloquear el bucle.
  const float millivolts = (float)analogReadMilliVolts(PIN_LM35);
  const float instant = millivolts / LM35_MV_PER_DEGREE;
  if (firstTempRead) {
    tempC = instant;
    firstTempRead = false;
  } else {
    tempC += (instant - tempC) * 0.25f;
  }

  ldrRaw = averageAnalog(PIN_LDR, 4);
  gasRaw = averageAnalog(PIN_MQ2, 4);

  led2On = ldrRaw < LDR_LED2_THRESHOLD;
  digitalWrite(PIN_LED2, led2On ? HIGH : LOW);

  // MQ-2: precalentamiento de 20 s e histéresis 1500 / 1300.
  if (now < GAS_WARMUP_MS) {
    gasAlert = false;
  } else if (!gasAlert && gasRaw > GAS_ALERT_ON) {
    gasAlert = true;
  } else if (gasAlert && gasRaw < GAS_ALERT_OFF) {
    gasAlert = false;
  }

  updateParasolAutomation();
  updateFanAutomation();
}

void readSound(uint32_t now) {
  // El LM393 pone D0 en LOW al detectar sonido. Se cuenta el flanco de bajada
  // (no el nivel) para no sumar miles de lecturas mientras sigue en LOW.
  const bool pinLow = digitalRead(PIN_KY038) == LOW;
  if (pinLow && !lastSoundLow) soundPulses++;
  lastSoundLow = pinLow;

  if (now - soundWindowStart < SOUND_WINDOW_MS) return;

  soundLoud = soundPulses >= SOUND_ACTIVE_PULSES;
  soundLevel = min<int>(soundPulses * SOUND_LEVEL_STEP, SOUND_LEVEL_MAX);
  soundPulses = 0;
  soundWindowStart = now;
  updateBabyLogic(now, soundLoud);
}

// ----------------------------- Telemetría JSON ------------------------------
void buildStateJson(String &payload) {
  StaticJsonDocument<384> doc;
  doc["temp"] = roundf(tempC * 10.0f) / 10.0f;
  doc["ldr"] = ldrRaw;
  doc["gas"] = gasRaw;
  doc["sound"] = soundLevel;
  doc["led1"] = led1On ? 1 : 0;
  doc["led2"] = led2On ? 1 : 0;
  doc["fan"] = fanUI;
  doc["parasol"] = parasolAngle;
  doc["temp_setpoint"] = tempSetpoint;
  doc["baby_alert"] = babyAlert;
  doc["gas_alert"] = gasAlert;
  serializeJson(doc, payload);
}

void broadcastState() {
  String payload;
  buildStateJson(payload);
  ws.textAll(payload);
}

// ------------------------------ Comandos web --------------------------------
void toggleParasol() {
  parasolManual = true;
  parasolTarget = (parasolTarget >= 180) ? 0 : 180;
}

void clearBabyAlert() {
  babyAlert = false;
  // Sin rearme inmediato: hacen falta 2 s de silencio desde este momento.
  babyArmed = false;
  babyQuietTracking = false;
  cryingMs = 0;
}

void handleCommand(const char *action, JsonDocument &doc) {
  if (strcmp(action, "toggle_led1") == 0) {
    led1On = !led1On;
    digitalWrite(PIN_LED1, led1On ? HIGH : LOW);

  } else if (strcmp(action, "set_fan") == 0) {
    const int value = constrain(doc["value"] | fanUI, 0, 100);
    fanManual = true;
    applyFan(value);

  } else if (strcmp(action, "toggle_parasol") == 0) {
    toggleParasol();

  } else if (strcmp(action, "set_temp_setpoint") == 0) {
    float value = doc["value"] | tempSetpoint;
    value = constrain(value, TEMP_SETPOINT_MIN, TEMP_SETPOINT_MAX);
    tempSetpoint = roundf(value * 2.0f) / 2.0f;  // Paso de 0.5 °C

  } else if (strcmp(action, "clear_baby_alert") == 0) {
    clearBabyAlert();
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

  const char *action = doc["action"] | "";
  handleCommand(action, doc);
  broadcastState();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    String payload;
    buildStateJson(payload);
    client->text(payload);
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len);
  }
}

// --------------------------------- Setup ------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  pinMode(PIN_KY038, INPUT);  // D0 digital del KY-038
  pinMode(PIN_FAN_PWM, OUTPUT);
  pinMode(PIN_FAN_DIR, OUTPUT);
  digitalWrite(PIN_LED1, LOW);
  digitalWrite(PIN_LED2, LOW);
  digitalWrite(PIN_FAN_DIR, LOW);  // Dirección fija del L9110S

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LM35, ADC_11db);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
  analogSetPinAttenuation(PIN_MQ2, ADC_11db);
  lastSoundLow = (digitalRead(PIN_KY038) == LOW);

  ledcSetup(FAN_CHANNEL, FAN_FREQ, FAN_RESOLUTION);
  ledcAttachPin(PIN_FAN_PWM, FAN_CHANNEL);
  ledcWrite(FAN_CHANNEL, 0);

  // El servo reserva el timer 0; el motor usa el canal 2 (timer 1).
  ESP32PWM::allocateTimer(0);
  parasolServo.setPeriodHertz(50);
  parasolServo.attach(PIN_SERVO, 500, 2400);
  parasolServo.write(parasolAngle);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] Error montando LittleFS");
    return;
  }

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[WiFi] SoftAP en http://");
  Serial.println(WiFi.softAPIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();

  const uint32_t now = millis();
  lastSensorRead = now;
  lastServoStep = now;
  lastTelemetry = now;
  soundWindowStart = now;

  readSensors(now + SENSOR_MS);  // Primea temp, LDR y MQ-2 antes de aceptar clientes
  Serial.println("[APP] Servidor y WebSocket /ws listos.");
}

// ---------------------------------- Loop ------------------------------------
void loop() {
  const uint32_t now = millis();

  ws.cleanupClients();
  readSound(now);
  readSensors(now);
  updateServoSweep(now);

  if (now - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = now;
    broadcastState();
  }
}
