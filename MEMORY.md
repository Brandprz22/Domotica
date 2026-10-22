# MEMORY.md — Domótica ESP32 (Maqueta C-137)
Memoria del proyecto entre sesiones. Máximo ~50 líneas: resume o elimina lo que ya no aporte.

## Estado actual
- Firmware ESP32 con SoftAP (`Rick_Lab_AP`), LittleFS y WebSocket `/ws`.
- Hito: integración de LM35, LDR, servomotor SG90 y motorreductor PWM, conservando el control de iluminación.
- Pines: LED 1 manual GPIO 17, LED 2 automático GPIO 16, servo GPIO 13, motor PWM GPIO 18 (LEDC 2), dirección GPIO 19, LM35 GPIO 34, LDR GPIO 35.
- Telemetría plana cada 1 s: `{"temp","ldr","led1","led2","fan","servo"}`.

## Decisiones (y por qué)
- **LM35 a 5 V:** su rango recomendado es 4–30 V; la salida máxima (1.5 V a 150 °C) es segura para el ADC de 3.3 V.
- **LDR a 3.3 V con pull-down de 10 kΩ:** oscuridad = ADC bajo (<800) = LED 2 encendido. Evita sobrevoltaje en GPIO 35.
- **ADC1 (GPIO 34/35):** no colisiona con el Wi-Fi; se usa atenuación 11 dB y 12 bits.
- **Timer LEDC 0 reservado para el servo:** `ESP32PWM::allocateTimer(0)` evita que ESP32Servo compita con el motor, que usa el canal 2 (timer 1) a 1 kHz.
- **Motor con arranque asistido:** 0 % = duty 0; 1–100 % se mapea a 75–255 para vencer la inercia del motor 130.
- **Servo sin slider:** solo botones 0°/90°/180° e indicador numérico, para diferenciarlo del control PWM del motor.
- **Frontend nativo sin frameworks ni CDNs:** mantiene el peso de `/data` bajo 40 KB y funciona en red local aislada.

## Aprendizajes y errores a evitar
- Alimentar servo y motor desde fuente externa con GND común; no usar el regulador del DevKit.
- Los navegadores móviles pueden desviar el tráfico local de `192.168.4.1` si los datos móviles están activos.
- No guardar documentación en `/data`: `uploadfs` empaqueta todo en LittleFS.
- El umbral LDR estricto en 800 puede parpadear cerca del límite; si ocurre, añadir histéresis.

## Próximos pasos
- Subir firmware y filesystem, y validar LEDs, LDR, motor, servo y telemetría desde el celular.
- Mantener el alcance mínimo: sin módulos ni sensores adicionales no solicitados.
