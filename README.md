# 🐾 Sistema IoT Pet (Server Based) 

Este proyecto implementa un sistema inteligente de dispensación de alimentos/agua para mascotas, utilizando un microcontrolador **ESP32** para monitorear el ambiente y la presencia de animales, y comunicarse con un **servidor externo** (probablemente con IA o lógica de control) para determinar la acción a tomar.

---

## 📁 Estructura del Proyecto

| Archivo | Descripción | Estado |
| :--- | :--- | :---: |
| **`SensoresPET.ino`** | **🔴 ARCHIVO PRINCIPAL** - Versión optimizada y unificada del sistema (v4.0). Incluye telemetría unificada cada 2 segundos, sensor de nivel de agua y código más limpio. | ⭐ Principal |
| `SensoresActuadores_I2C.ino` | Versión anterior del sistema (v3.0). Contiene la implementación base con calibración dinámica de luz. Útil como referencia. | 📦 Legado |
| `README.md` | Documentación del proyecto. | 📄 Docs |

### Diferencias principales entre versiones:

| Característica | `SensoresPET.ino` (v4.0) | `SensoresActuadores_I2C.ino` (v3.0) |
| :--- | :--- | :--- |
| Telemetría | ✅ Unificada (cada 2s) | ❌ No incluida |
| Sensor de Nivel | ✅ GPIO 5 | ❌ No soportado |
| Velocidad dispensado | 20 mL/s configurable | No especificado |
| Brown-out Disable | ❌ No necesario | ✅ Incluido |

> **💡 Recomendación:** Usar `SensoresPET.ino` para nuevas implementaciones.

---

## ⚙️ Componentes de Hardware Utilizados

El sistema está diseñado para interactuar con los siguientes sensores y actuadores:

| Componente | Pin ESP32 (GPIO) | Función | Código (Librería) |
| :--- | :--- | :--- | :--- |
| **Sensor de Distancia** (Ultrasonido) | Trigger: **13** / Echo: **35** | Detecta la presencia y distancia de una mascota. | Función `medirDistancia()` |
| **Relé de Bomba** | **14** | Controla el encendido/apagado de la bomba dispensadora. | Lógica Invertida (LOW = ON) |
| **Relé de Luces** | **27** | Controla la iluminación ambiental automática. | Lógica Invertida (LOW = ON) |
| **Sensor de Temperatura** | **15** | Mide la temperatura del agua (DS18B20). | [cite_start]`DallasTemperature` [cite: 5] |
| **Sensor de Luz (LDR)** | **34** (ADC) | Detecta el nivel de iluminación ambiental para controlar las luces. | [cite_start]Calibración dinámica [cite: 6, 7] |
| **Pantalla OLED** (128x64) | SDA/SCL (Default **21/22**) | Muestra el estado del sistema y la cuenta regresiva de dispensación. | [cite_start]`Adafruit_SSD1306`, I2C (Dirección 0x3C) [cite: 4, 5] |

---

## 💻 Arquitectura y Comunicación

Este proyecto se basa en un modelo **cliente-servidor** para externalizar la lógica de decisión, potencialmente una lógica compleja como el reconocimiento de imagen (IA).

### 1. Conectividad

* [cite_start]Utiliza la librería `WiFi.h` para conectarse a la red Wi-Fi[cite: 1].
* [cite_start]Credenciales definidas en el código (`ssid` y `password`)[cite: 2].

### 2. Máquina de Estados

[cite_start]El programa opera a través de una Máquina de Estados para gestionar el flujo de trabajo: [cite: 6]
* **`IDLE`**: Esperando la detección de una mascota (función `loopIdle()`).
* **`ESPERANDO_SERVIDOR`**: Esperando la respuesta del servidor sobre la identidad del animal detectado (función `loopEsperandoRespuesta()`).
* **`DISPENSANDO`**: Ejecutando la acción del relé de la bomba (función `procesarDispensado()`).
* [cite_start]**`COOLDOWN_STATE`**: Periodo de espera de 12 segundos para evitar detecciones repetidas inmediatas[cite: 33].

### 3. Comunicación HTTP

[cite_start]La comunicación con el servidor (`http://10.183.54.122:5000`) se realiza mediante peticiones **GET**[cite: 3, 48].

| End-point | Método | Descripción | Uso en el Código |
| :--- | :--- | :--- | :--- |
| `/trigger_detection` | GET | [cite_start]Notifica al servidor que una mascota ha sido detectada[cite: 48]. | [cite_start]`enviarTriggerAServidor()` [cite: 48] |
| `/check_command` | GET | [cite_start]Consulta la respuesta del servidor (Ej: "PERRO", "GATO", "DESCONOCIDO")[cite: 52]. | [cite_start]`consultarComandoServidor()` [cite: 52] |
| `/report_status` | GET | Envía reportes de estado (luces, bomba) al servidor. | `reportarEstado()` |

---

## 🛠️ Configuración Inicial

Para que este código funcione, debe modificar las siguientes variables al inicio del archivo `.ino`:

1.  **Credenciales Wi-Fi:**
    ```arduino
    const char* ssid = "TU_WIFI";
    const char* password = "TU_CONTRASEÑA";
    ```
2.  **Dirección del Servidor:**
    ```arduino
    String serverIP = "LA_IP_DE_TU_PC";
    int serverPort = 5000;
    ```
    *Asegúrese de que el servidor esté en ejecución y la IP sea accesible desde la red local.*

---

## 🌟 Características Clave

* [cite_start]**Dispensación Inteligente:** La duración de la dispensación varía según la respuesta del servidor (5 segundos para "PERRO", 2 segundos para "GATO")[cite: 44, 45].
* [cite_start]**Control de Iluminación Automática:** Utiliza un LDR con un sistema de **calibración dinámica** que ajusta los umbrales de luz mínima y máxima para controlar el relé de luces[cite: 66, 71].
* [cite_start]**Seguridad y Timeout:** Incluye un *timeout* de 15 segundos (`TIMEOUT_SERVIDOR`) para evitar que el sistema se quede bloqueado esperando la respuesta del servidor[cite: 8, 40].
* [cite_start]**Monitorización:** Mide la temperatura del agua con el sensor DS18B20[cite: 64].
