# AGENTS.md — Domótica ESP32 (Maqueta C-137)
Interfaz web ligera alojada en LittleFS de un ESP32 (DOIT DevKit V1) para monitoreo y control en tiempo real vía WebSockets. Proyecto didáctico para Arquitectura de Computadoras: código claro, modular y fácil de mantener.

## Stack y estructura
- HTML5, CSS3 y JavaScript moderno (ES6+) puros: sin frameworks, npm, bundlers, CDNs ni librerías externas.
- Todos los archivos de la interfaz residen exclusivamente en `/data`:
  - `data/index.html` (estructura semántica; debe enlazar `<script src="connect.js"></script>`).
  - `data/style.css` (estilos, layout y variables de tema).
  - `data/connect.js` (conexión WebSocket, renderizado de telemetría y eventos).
- Debe funcionar en red local aislada (SoftAP `192.168.4.1` sin salida a internet).

## Ámbito estricto de permisos
- Trabaja exclusivamente dentro de la carpeta `data/`.
- Queda **estrictamente prohibido** crear, modificar, borrar o renombrar archivos fuera de `data/` (como `src/main.cpp`, `platformio.ini` o cualquier archivo en la raíz), a menos que el usuario lo solicite de manera explícita.

## Convenciones de diseño y UI
- Paleta inicial clara (identidad ESCOM): fondo blanco/gris tenue (`#F4F6F9`), azul marino institucional (`#003366`) para títulos y acentos en azul celeste (`#00A3E0`) para elementos activos[cite: 1, 2, 3].
- Todo color, radio de borde y espaciado debe definirse mediante variables CSS en `:root` para facilitar la migración futura a modo oscuro.
- Tipografía del sistema obligatoria: usar exclusivamente la pila nativa (`font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif`). Queda estrictamente prohibido cualquier `@import` o URL externa en CSS.
- Diseño modular en tarjetas (*cards*) con esquinas redondeadas (12 a 16 px) y sombras suaves[cite: 1, 2].
- Métricas principales en números grandes y legibles tipo KPI (Temp, Gas, Luz, Presencia)[cite: 1].
- Controles táctiles amplios (mínimo 44 px de alto) con respuesta visual inmediata[cite: 1, 2, 3].
- Todos los textos de la interfaz en español.

## Comunicación y datos (WebSockets)
- Endpoint: `ws://${window.location.hostname}/ws`.
- Telemetría entrante (ESP32 → Web): JSON con `{ temp, gas, ldr, sound, ir, lights: { lab, morty, sala, ext } }`.
- Comandos salientes (Web → ESP32): JSON tipo `{ action: "light", zone: "lab" }` o `{ action: "fan", speed: 80 }`.
- Alerta crítica: Si `sound === 0` (micrófono activado) o gas elevado, mostrar banner de alerta, disparar `navigator.vibrate` y emitir pitido con la API nativa `AudioContext`.
- Reconexión automática si el socket se interrumpe.

## Memoria
- Al empezar cualquier tarea, lee `MEMORY.md` para conocer el estado actual y decisiones tomadas.
- Al terminar cada tarea, actualiza `MEMORY.md`: estado actual, decisiones con su porqué y aprendizajes.
- Mantén `MEMORY.md` breve (máximo ~50 líneas).
- Nunca guardes datos sensibles ni contraseñas reales.

## Forma de trabajar
- Haz solo lo que se pide: no agregues pantallas o funciones no solicitadas.
- Cambios pequeños, limpios y enfocados; no reescribas lo que ya funciona.
- Al terminar, resume qué has cambiado y los pasos exactos para probarlo.

## Límites
- ✅ Siempre: trabajar estrictamente dentro de `/data`, actualizar `MEMORY.md`, usar variables CSS, usar tipografías del sistema y mantener cero dependencias externas.
- ⚠️ Pregunta antes: alterar la estructura o nombres de las claves del JSON de WebSockets.
- 🚫 Nunca: modificar archivos fuera de `/data`, instalar paquetes npm, usar reglas `@import` en CSS o meter librerías por CDN (Bootstrap, Tailwind, Google Fonts, FontAwesome, Chart.js).
- 📦 Restricción de peso: el peso total combinado de todos los archivos en `/data` no debe superar los 40 KB bajo ninguna circunstancia.

## Verificación
- Prueba local de interfaz: abrir `data/index.html` en el navegador (mockeando el WebSocket si es necesario).
- Despliegue al microcontrolador: comando `pio run --target uploadfs`.

## Buenas prácticas de Frontend Embebido
- **DOM quirúrgico:** Ante eventos de WebSocket, actualiza únicamente nodos específicos con `.textContent` o `.classList.toggle()`. Prohibido rehacer componentes con `.innerHTML`.
- **Rendimiento:** Evita bucles pesados en JavaScript. Si graficas datos, hazlo sobre un `<canvas>` nativo reutilizando el mismo contexto de renderizado.
- **Mobile-First y Touch:** Diseña primero pensando en la pantalla de un smartphone. Botones con altura mínima de 44px, espaciado adecuado para dedos y `touch-action: manipulation`.
- **CSS Modular:** Usa Flexbox/Grid y variables en `:root`. No uses hacks de CSS ni propiedades experimentales que no estén soportadas de forma estándar.