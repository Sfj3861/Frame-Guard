# Frame-Guard — Geode para Android

**Objetivo:** Geometry Dash Android 2.2081 · Geode SDK 5.11.2 · Android 64 bits.  
**Versión de desarrollo:** v0.13.0.

Frame-Guard es una superposición de diagnóstico. Dibuja geometría estimada de colisión, límites del jugador y una trayectoria futura aproximada, y registra eventos de entrada para ayudar a analizar el comportamiento del juego.

## Funciones

- **Hitboxes de objetos:** contornos para peligros, sólidos, pendientes y una selección de objetos interactivos. Los objetos decorativos se filtran; la lista de tipos compatibles no equivale a la lógica interna de colisión del juego.
- **Alineación con la capa del nivel:** la superposición se añade a la capa de objetos para compartir sus transformaciones de cámara/scroll en vez de dibujarse como una capa independiente de la escena.
- **Límites del jugador:** muestra el rectángulo actual, la caja orientada cuando está disponible y una caja interior aproximada.
- **Trayectoria aproximada:** proyecta hasta 120 pasos con la velocidad y el estado actual de entrada. Colorea en rojo una sección cuando su caja proyectada intersecta geometría compatible. No ejecuta la física completa ni simula portales, triggers, colisiones futuras o cambios de modo.
- **HUD de FrameGuard:** muestra el número de frame y estadísticas del último evento de entrada.
- **Contador de FPS:** estimación a partir del tiempo entre actualizaciones; también muestra el presupuesto de ticks correspondiente al límite de espera configurado.
- **Menú flotante bilingüe:** controles en inglés o español para activar/desactivar hitboxes, límites del jugador, trayectoria, HUD, información, FPS y el planificador experimental.
- **Ajustes visuales:** opacidad y margen visual de seguridad ajustables.
- **Telemetría CSV:** exporta los últimos 16 eventos de entrada a la carpeta de guardado del mod de Geode.
- **Atajo de teclado:** la tecla F abre/cierra el menú de forma predeterminada; se puede cambiar en los ajustes de Geode.
- **Planificador experimental de salto:** opción desactivada por defecto que puede aplazar eventos de pulsación/liberación de salto según una estimación simplificada de colisión. El límite de espera se configura entre 0 y 200 ms.

## Limitaciones importantes

- **No está validado en partida.** Que el proyecto compile o que el paquete pase las comprobaciones de archivo no demuestra que el mod funcione correctamente en el dispositivo.
- La trayectoria es una aproximación visual, no una reproducción del motor de física de Geometry Dash ni una garantía de que una ruta sea segura.
- El planificador de salto es experimental, puede cambiar el momento de entrada y puede equivocarse. Déjalo desactivado si causa entradas inesperadas; está desactivado por defecto.
- Las hitboxes mostradas son diagnósticas y pueden no coincidir con todos los casos especiales del motor.
- El menú y las capas deben comprobarse en el dispositivo real, incluidas resolución, zoom, scroll, dual, portales y cambios de modo.
- Frame-Guard no debe tratarse como una herramienta para garantizar completaciones ni como sustituto de practicar el nivel.

## Instalación

Cuando haya una compilación CI correcta, descarga el archivo `.geode` de la sección **Releases** del repositorio e instálalo con Geode para Android de 64 bits. Sustituye la versión anterior de Frame-Guard al actualizar.

- [Releases](https://github.com/Sfj3861/Frame-Guard/releases)
- [Compilaciones y registros de CI](https://github.com/Sfj3861/Frame-Guard/actions)

No descargues una compilación si el flujo de CI no ha terminado correctamente.

## Compilar desde el código

Con el SDK y las herramientas de Android configuradas:

```bash
geode sdk install-binaries -p android64
geode build -p android64
```

El flujo de GitHub Actions valida `mod.json`, compila para Android64, comprueba que se haya generado exactamente un paquete, verifica la estructura ZIP y que contenga `mod.json`, calcula su SHA-256 y publica una versión preliminar. Una compilación satisfactoria sigue necesitando pruebas dentro de Geometry Dash Android 2.2081.

## Historial de desarrollo

### v0.13.0
- La superposición se mueve a la capa de objetos para compartir las transformaciones del nivel.
- La caché de objetos de colisión se invalida al reiniciar el nivel.
- Las entradas diferidas se descartan si el jugador ha muerto, y la cola se limpia al salir del nivel.
- CI valida los metadatos y el paquete generado antes de publicarlo.

### v0.12.0
- Se añadió un planificador experimental y opcional para eventos de salto.
- Usa una estimación simplificada; no modela toda la física, colisiones ni triggers. No está probado en partida.

### v0.11.0
- Controles bilingües, exportación CSV y presupuesto configurable de espera de entrada.
