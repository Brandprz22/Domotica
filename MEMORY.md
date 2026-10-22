# MEMORY.md — Domótica ESP32 (Maqueta C-137)
Memoria del proyecto entre sesiones. Máximo ~50 líneas: resume o elimina lo que ya no aporte.

## Estado actual
- Firmware base en ESP32 configurado con SoftAP (`Rick_Lab_AP`) y LittleFS montado.
- WebSocket `/ws` funcional para sincronización bidireccional en tiempo real.
- Hito: control funcional de 2 LEDs independientes (GPIO 16 y GPIO 17), broadcast `{"lights":{"led1":0/1,"led2":0/1}}`.
- Frontend mínimo en `/data` con barra superior, estado WS y dos tarjetas (LED 1 y LED 2).

## Decisiones (y por qué)
- **Descarte de LabVIEW:** El ESP32 es autónomo; la interfaz web por WebSockets resuelve la telemetría, gráficas y control móvil sin depender de software pesado en la PC.
- **Frontend nativo sin frameworks:** HTML/CSS/JS puros para mantener el peso total por debajo de 40 KB, logrando carga inmediata sobre la Flash del microcontrolador.
- **Sin CDNs:** Todo icono o estilo es local/SVG nativo porque la maqueta opera en una red Wi-Fi escolar sin conexión a internet.
- **Tema claro inicial con variables `:root`:** Facilita la presentación académica con colores institucionales ESCOM y permite cambiar a tema oscuro invirtiendo los valores en CSS.
- **Comando toggle mínimo:** `{ action: "toggle", target: "led1|led2" }`; el firmware también acepta claves `toggle`, `zone`, `led` y `name` para tolerancia.

## Aprendizajes y errores a evitar
- Los navegadores móviles desactivan o desvían el tráfico local de `192.168.4.1` si los datos móviles (4G/5G) están encendidos.
- No guardar archivos de documentación o notas en `/data`, ya que `uploadfs` empaqueta todo a la Flash y reduce el espacio disponible.
- Usar siempre `AudioContext` nativo para alertas audibles en lugar de cargar archivos `.mp3` pesados en LittleFS.

## Próximos pasos
- Subir a Flash con `uploadfs` y validar encendido/apagado desde el celular.
- Mantener el frontend sin sensores, simulaciones ni módulos adicionales.
