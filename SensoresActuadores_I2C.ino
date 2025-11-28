#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>             // <--- AGREGADO PARA I2C
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// =========================================================
// ==================== CREDENCIALES WIFI ==================
// =========================================================
const char* ssid = "MATHEW_LAP 2899";  // <--- TU WIFI
const char* password = "50X&272g";     // <--- TU CONTRASEÑA
String serverIP = "10.183.54.122";    // <--- IP DE TU PC/SERVIDOR
int serverPort = 5000;

// =========================================================
// ==================== CONFIGURACIÓN PINES =================
// =========================================================

// --- Actuadores y Sensores ---
#define PIN_TRIGGER       13
#define PIN_ECHO          35
#define PIN_RELE_BOMBA    14 // Lógica Invertida (LOW = ON)
#define PIN_RELE_LUCES    27 // Lógica Invertida (LOW = ON)
#define PIN_DS18B20       15
#define PIN_LDR           34

// --- OLED I2C (CONFIGURACIÓN MODIFICADA) ---
// Pines por defecto ESP32: SDA = GPIO 21, SCL = GPIO 22
#define OLED_RESET     -1      // Reset compartido con VCC o no usado (-1)
#define SCREEN_ADDRESS 0x3C    // Dirección I2C (0x3C o 0x3D)
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64

// Constructor para I2C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Objetos Globales ---
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensorTemp(&oneWire);

// =========================================================
// ==================== VARIABLES GLOBALES ==================
// =========================================================

// Estados del Sistema
enum Estado { IDLE, ESPERANDO_SERVIDOR, DISPENSANDO, COOLDOWN_STATE };
Estado estadoActual = IDLE;

// Variables de Control
float temperaturaAgua = -99.0;
bool lucesEncendidas = false;
String ultimaMascota = "---";

// Calibración Luz
int luzMax = 0;
int luzMin = 4095;
unsigned long ultimaCalibracion = 0;

// Temporizadores
unsigned long tiempoInicioEspera = 0;
const int TIMEOUT_SERVIDOR = 15000; // 15 segundos max esperando respuesta
const int DISTANCIA_MAX = 30;

// =========================================================
// ==================== LOGS SERIAL ========================
// =========================================================
void logSerial(String categoria, String mensaje) {
  Serial.print("["); Serial.print(categoria); Serial.print("] "); Serial.println(mensaje);
}

// =========================================================
// ==================== SETUP ==============================
// =========================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  
  logSerial("INICIO", "Arrancando Sistema IoT Pet v3.0 (Server Based)");

  // 1. Configurar Pines
  pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELE_BOMBA, OUTPUT);
  pinMode(PIN_RELE_LUCES, OUTPUT);
  
  // Apagar todo (Relés lógica invertida: HIGH = APAGADO)
  digitalWrite(PIN_RELE_BOMBA, HIGH);
  digitalWrite(PIN_RELE_LUCES, HIGH);

  // 2. Iniciar OLED I2C (MODIFICADO)
  // SSD1306_SWITCHCAPVCC = generar voltaje de pantalla desde 3.3V interno
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { 
    logSerial("ERROR", "Fallo al iniciar OLED I2C (Revisa cables SDA/SCL)");
    // Intentar forzar reinicio o bucle infinito
    while(1);
  }
  display.clearDisplay();
  mostrarTexto("Iniciando...", "Hardware OK");
  delay(1000);

  // 3. Iniciar Sensores
  sensorTemp.begin();
  
  // 4. Conexión WiFi
  conectarWiFi();
}

void conectarWiFi() {
  // Si ya está conectado, salimos para no interrumpir
  if (WiFi.status() == WL_CONNECTED) return;

  mostrarTexto("Conectando", "WiFi...");
  logSerial("WIFI", "Reseteando hardware WiFi...");

  // PASO CLAVE: Detener cualquier intento anterior
  WiFi.disconnect(true);  // true = borrar credenciales guardadas
  delay(1000);            // Esperar a que el hardware se limpie
  WiFi.mode(WIFI_STA);    // Forzar modo Estación
  delay(100);

  logSerial("WIFI", "Iniciando conexión a: " + String(ssid));
  WiFi.begin(ssid, password);

  int intentos = 0;
  while(WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
    intentos++;
    
    // Si falla mucho tiempo, hacemos un "Hard Reset" del intento
    if(intentos > 20) { // 10 segundos aprox
      logSerial("WIFI", "Tarda mucho. Reiniciando intento...");
      WiFi.disconnect(true);
      delay(1000);
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, password);
      intentos = 0;
    }
  }
  
  Serial.println();
  logSerial("WIFI", "Conectado exitosamente! IP: " + WiFi.localIP().toString());
  mostrarTexto("WiFi", "Conectado");
  delay(1000);
}

// =========================================================
// ==================== LOOP PRINCIPAL =====================
// =========================================================
void loop() {
  // Tareas siempre activas (Background)
  if(WiFi.status() != WL_CONNECTED) conectarWiFi();
  actualizarTemperatura();
  calibrarLuzAutomaticamente();
  controlarLuces();

  // Máquina de Estados Principal
  switch (estadoActual) {
    case IDLE:
      loopIdle();
      break;

    case ESPERANDO_SERVIDOR:
      loopEsperandoRespuesta();
      break;

    case DISPENSANDO:
      // El estado se maneja dentro de la función de dispensado
      break;
      
    case COOLDOWN_STATE:
      display.clearDisplay();
      mostrarInfoReposo("Enfriando...");
      delay(5000); 
      // Pequeño delay bloqueante simple para el cooldown, 
      // o usar millis si quieres multitarea real.
      // Por simplicidad del Borrador2, esperamos aquí un poco y volvemos a IDLE
      static unsigned long inicioCooldown = 0;
      if (inicioCooldown == 0) inicioCooldown = millis();
      
      if (millis() - inicioCooldown > 12000) {
        logSerial("ESTADO", "Sistema listo para nueva detección");
        estadoActual = IDLE;
        inicioCooldown = 0;
      }
      break;
  }
  
  // Pequeño delay para estabilidad
  delay(100);
}

// =========================================================
// ==================== LÓGICA DE ESTADOS ==================
// =========================================================

void loopIdle() {
  mostrarInfoReposo("");
  
  int distancia = medirDistancia();
  
  // Si detecta algo cerca y válido
  if (distancia > 0 && distancia < DISTANCIA_MAX) {
    logSerial("SENSOR", "Animal detectado a " + String(distancia) + " cm");
    mostrarTexto("Detectado!", "Enviando...");
    
    // Paso 1: Enviar Trigger al Servidor
    if (enviarTriggerAServidor()) {
      estadoActual = ESPERANDO_SERVIDOR;
      tiempoInicioEspera = millis();
      logSerial("ESTADO", "Cambiando a ESPERANDO_SERVIDOR");
    } else {
      logSerial("ERROR", "No se pudo comunicar con el servidor");
      mostrarTexto("Error", "Servidor");
      delay(2000);
    }
  }
}

void loopEsperandoRespuesta() {
  // Timeout de seguridad
  if (millis() - tiempoInicioEspera > TIMEOUT_SERVIDOR) {
    logSerial("TIMEOUT", "El servidor no respondió a tiempo");
    mostrarTexto("Error", "Timeout");
    delay(2000);
    estadoActual = IDLE;
    return;
  }

  // Consultar cada 1 segundo (Polling)
  static unsigned long ultimaConsulta = 0;
  if (millis() - ultimaConsulta > 1000) {
    ultimaConsulta = millis();
    
    // Feedback visual
    mostrarTexto("Analizando...", String((TIMEOUT_SERVIDOR - (millis() - tiempoInicioEspera))/1000) + "s");
    
    String comando = consultarComandoServidor();
    logSerial("SERVIDOR", "Respuesta recibida: " + comando);

    if (comando == "PERRO") {
      ultimaMascota = "Perro";
      procesarDispensado(5000, "PERRO"); // 5 segundos
    } 
    else if (comando == "GATO") {
      ultimaMascota = "Gato";
      procesarDispensado(2000, "GATO"); // 2 segundos
    }
    else if (comando == "DESCONOCIDO") {
       logSerial("IA", "Animal no reconocido con suficiente certeza");
       mostrarTexto("No", "Reconocido");
       delay(2000);
       estadoActual = IDLE; // Volver a intentar
    }
    else if (comando == "ESPERA" || comando == "PROCESANDO") {
       // Seguir esperando
       Serial.print(".");
    }
  }
}

// =========================================================
// ==================== COMUNICACIÓN HTTP ==================
// =========================================================

bool enviarTriggerAServidor() {
  HTTPClient http;
  String url = "http://" + serverIP + ":" + String(serverPort) + "/trigger_detection";
  logSerial("HTTP", "GET " + url);
  
  http.begin(url);
  int httpCode = http.GET();
  http.end();
  
  if (httpCode == 200) {
    logSerial("HTTP", "Trigger enviado OK (200)");
    return true;
  } else {
    logSerial("HTTP", "Fallo Trigger. Code: " + String(httpCode));
    return false;
  }
}

String consultarComandoServidor() {
  HTTPClient http;
  String url = "http://" + serverIP + ":" + String(serverPort) + "/check_command";
  
  http.begin(url);
  int httpCode = http.GET();
  String payload = "ERROR";
  
  if (httpCode == 200) {
    payload = http.getString();
    payload.trim();
    payload.replace("\"", ""); // Quitar comillas si el JSON las trae
  } else {
    logSerial("HTTP", "Error consultando comando: " + String(httpCode));
  }
  http.end();
  return payload;
}

void reportarEstado(String componente, String estado) {
  // Fire and forget (no bloqueante, no esperamos respuesta critica)
  HTTPClient http;
  String url = "http://" + serverIP + ":" + String(serverPort) + "/report_status?component=" + componente + "&status=" + estado;
  http.begin(url);
  http.GET();
  http.end();
  logSerial("REPORT", componente + " -> " + estado);
}

// =========================================================
// ==================== ACCIONES FÍSICAS ===================
// =========================================================

void procesarDispensado(int duracionMs, String tipoAnimal) {
  estadoActual = DISPENSANDO;
  
  Serial.println("\n--- INICIANDO DISPENSADO ---");
  logSerial("ACCION", "Dispensando para: " + tipoAnimal);
  
  // 1. Encender Bomba
  digitalWrite(PIN_RELE_BOMBA, LOW); // LOW = ENCENDIDO
  //reportarEstado("BOMBA", "ON");
  
  // 2. Cuenta regresiva en pantalla
  int segundos = duracionMs / 1000;
  for (int i = segundos; i > 0; i--) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 5);
    display.println(tipoAnimal);
    
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println("Dispensando agua...");
    
    display.setTextSize(2); 
    display.setCursor(50, 45); 
    display.print(i); display.println("s");
    display.display();
    
    logSerial("BOMBA", "Restante: " + String(i) + "s");
    delay(1000);
  }

  // 3. Apagar Bomba
  digitalWrite(PIN_RELE_BOMBA, HIGH); // HIGH = APAGADO
  //reportarEstado("BOMBA", "OFF");
  logSerial("ACCION", "Dispensado finalizado");
  
  mostrarTexto("Servido", "Gracias!");
  delay(2000);
  
  // 4. Ir a Cooldown
  estadoActual = COOLDOWN_STATE;
}

// =========================================================
// ==================== AUXILIARES SENSORES ================
// =========================================================

int medirDistancia() {
  digitalWrite(PIN_TRIGGER, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIGGER, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIGGER, LOW);
  long t = pulseIn(PIN_ECHO, HIGH, 30000);
  return (t == 0) ? -1 : (t / 2) / 29.1;
}

void actualizarTemperatura() {
  static unsigned long lastTime = 0;
  if (millis() - lastTime < 3000) return;
  lastTime = millis();

  sensorTemp.requestTemperatures();
  float t = sensorTemp.getTempCByIndex(0);
  if (t != DEVICE_DISCONNECTED_C && t > -50) {
    temperaturaAgua = t;
  }
}

void calibrarLuzAutomaticamente() {
  static unsigned long lastCalib = 0;
  if (millis() - lastCalib < 10000) return; // Cada 10s
  lastCalib = millis();

  int val = analogRead(PIN_LDR);
  if (val > luzMax) luzMax = val;
  if (val < luzMin) luzMin = val;
}

void controlarLuces() {
  bool oscuro = detectarOscuridad();
  
  if (oscuro && !lucesEncendidas) {
    digitalWrite(PIN_RELE_LUCES, LOW); // ON
    lucesEncendidas = true;
    logSerial("LUCES", "ON - Ambiente oscuro");
    reportarEstado("LUCES", "ON");
  } 
  else if (!oscuro && lucesEncendidas) {
    digitalWrite(PIN_RELE_LUCES, HIGH); // OFF
    lucesEncendidas = false;
    logSerial("LUCES", "OFF - Ambiente claro");
    reportarEstado("LUCES", "OFF");
  }
}

bool detectarOscuridad() {
  int valor = analogRead(PIN_LDR);
  // Simple histéresis dinámica
  int umbral = luzMin + (luzMax - luzMin) * 0.7; 
  if (luzMax - luzMin < 100) umbral = 2000; // Default por seguridad
  return valor > umbral;
}

// =========================================================
// ==================== AUXILIARES PANTALLA ================
// =========================================================

void mostrarTexto(String titulo, String subtitulo) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println(titulo);
  display.setCursor(0, 30);
  display.setTextSize(1);
  display.println(subtitulo);
  display.display();
}

void mostrarInfoReposo(String estadoTexto) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("IoT Pet System");
  display.setCursor(80, 0);
  display.println(estadoTexto);
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Info
  display.setCursor(0, 15);
  display.print("Agua: "); display.print(temperaturaAgua, 1); display.print("C");
  
  display.setCursor(0, 30);
  display.print("Luz: "); display.print(lucesEncendidas ? "ON" : "OFF");
  
  display.setCursor(0, 45);
  display.print("Ultima: "); display.print(ultimaMascota);

  display.display();
}