#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// =========================================================
// ==================== CREDENCIALES WIFI ==================
// =========================================================
const char* ssid = "MATHEW_LAP 2899";
const char* password = "50X&272g";
String serverIP = "10.202.9.122";  
int serverPort = 5000;

// =========================================================
// ==================== CONFIGURACIÓN PINES =================
// =========================================================

#define PIN_TRIGGER       13
#define PIN_ECHO          35
#define PIN_RELE_BOMBA    14 
#define PIN_RELE_LUCES    27 
#define PIN_DS18B20       15
#define PIN_LDR           34
#define PIN_SENSOR_NIVEL  5  

#define OLED_RESET     -1      
#define SCREEN_ADDRESS 0x3C    
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

OneWire oneWire(PIN_DS18B20);
DallasTemperature sensorTemp(&oneWire);

// =========================================================
// ==================== VARIABLES ==========================
// =========================================================

enum Estado { IDLE, ESPERANDO_SERVIDOR, DISPENSANDO, COOLDOWN_STATE };
Estado estadoActual = IDLE;

float temperaturaAgua = 0.0; 
bool lucesEncendidas = false;
String ultimaMascota = "---";

int luzMax = 0;
int luzMin = 4095;
const int ML_POR_SEGUNDO = 20; 

unsigned long tiempoInicioEspera = 0;
const int TIMEOUT_SERVIDOR = 15000; 
const int DISTANCIA_MAX = 30;

// Telemetría Unificada
unsigned long ultimaTelemetria = 0;
const int INTERVALO_TELEMETRIA = 2000; // Envío cada 2 segundos

void logSerial(String categoria, String mensaje) {
  Serial.print("["); Serial.print(categoria); Serial.print("] "); Serial.println(mensaje);
}

// =========================================================
// ==================== SETUP ==============================
// =========================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  logSerial("INICIO", "Sistema IoT Pet v4.0 (Full Unified)");

  pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELE_BOMBA, OUTPUT);
  pinMode(PIN_RELE_LUCES, OUTPUT);
  pinMode(PIN_SENSOR_NIVEL, INPUT_PULLUP); 
  
  digitalWrite(PIN_RELE_BOMBA, HIGH);
  digitalWrite(PIN_RELE_LUCES, HIGH);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { 
    while(1);
  }
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Iniciando...");
  display.display();
  delay(1000);

  sensorTemp.begin();
  sensorTemp.setWaitForConversion(false);
  conectarWiFi();
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  display.clearDisplay();
  display.setCursor(0,0); display.println("Conectando WiFi...");
  display.display();

  WiFi.disconnect(true); delay(1000); WiFi.mode(WIFI_STA); delay(100);
  WiFi.begin(ssid, password);

  int intentos = 0;
  while(WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    intentos++;
    if(intentos > 20) { 
      WiFi.disconnect(true); delay(1000); WiFi.mode(WIFI_STA); WiFi.begin(ssid, password); intentos = 0;
    }
  }
  logSerial("WIFI", "Conectado: " + WiFi.localIP().toString());
}

// =========================================================
// ==================== VISUALES ===========================
// =========================================================

void dibujarEncabezado() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("IoT Pet");
  display.setCursor(75, 0);
  display.print(temperaturaAgua, 1);
  display.print(" C");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
}

void mostrarInfoReposo(String estadoTexto) {
  dibujarEncabezado();
  display.setCursor(0, 15);
  display.print("Luz: "); display.println(lucesEncendidas ? "ON" : "OFF");
  display.setCursor(0, 27);
  display.print("Ultima: "); display.println(ultimaMascota);
  display.setCursor(0, 39);
  display.print("Estado: "); display.println(estadoTexto);

  if (digitalRead(PIN_SENSOR_NIVEL) == LOW) { // LLENO
     display.fillRect(0, 53, 128, 11, SSD1306_WHITE); 
     display.setTextColor(SSD1306_BLACK); 
     display.setCursor(10, 55);
     display.print("! TANQUE LLENO !");
  } else {
     display.setTextColor(SSD1306_WHITE);
     display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
     display.setCursor(0, 55);
     display.print("Nivel: OK");
  }
  display.display();
}

bool isTanqueLleno() {
  return (digitalRead(PIN_SENSOR_NIVEL) == LOW); 
}

// =========================================================
// ==================== FUNCION UNIFICADA DE ENVIO =========
// =========================================================

void enviarDatosUnificados(bool forzarEnvio) {
  // Solo envía si pasó el tiempo O si forzamos el envío (ej. cambio de luces)
  if (forzarEnvio || millis() - ultimaTelemetria > INTERVALO_TELEMETRIA) {
    ultimaTelemetria = millis();
    
    // 1. Recopilar TODOS los datos
    String nivelStr = isTanqueLleno() ? "LLENO" : "OK";
    String luzStr = lucesEncendidas ? "ON" : "OFF";
    String tempStr = String(temperaturaAgua, 1);
    
    // 2. Construir la MEGA URL
    String url = "http://" + serverIP + ":" + String(serverPort) + "/monitor_sensors" + 
                 "?temp=" + tempStr + 
                 "&level=" + nivelStr + 
                 "&light=" + luzStr + 
                 "&pet=" + ultimaMascota;

    // 3. Enviar y Loguear
    logSerial("HTTP", "GET " + url); 
    
    HTTPClient http; 
    http.begin(url); 
    int httpCode = http.GET(); 
    http.end();
    
    if (httpCode != 200) {
      // Opcional: Loguear si falla
      // logSerial("ERROR", "Fallo envio datos");
    }
  }
}

// =========================================================
// ==================== LOOP PRINCIPAL =====================
// =========================================================
void loop() {
  if(WiFi.status() != WL_CONNECTED) conectarWiFi();

  // 1. SEGURIDAD PRIORITARIA
  if (isTanqueLleno()) {
    gestionarErrorNivel(); 
    return; // Sale del loop, pero gestionarErrorNivel se encarga de reportar
  }

  actualizarTemperatura();
  calibrarLuzAutomaticamente();
  controlarLuces();
  
  // 2. REPORTE NORMAL (Cada 2 seg)
  enviarDatosUnificados(false); 

  switch (estadoActual) {
    case IDLE:
      loopIdle();
      break;
    case ESPERANDO_SERVIDOR:
      loopEsperandoRespuesta();
      break;
    case DISPENSANDO:
      break;
    case COOLDOWN_STATE:
      mostrarInfoReposo("Enfriando...");
      static unsigned long inicioCooldown = 0;
      if (inicioCooldown == 0) inicioCooldown = millis();
      if (millis() - inicioCooldown > 12000) {
        estadoActual = IDLE;
        inicioCooldown = 0;
      }
      delay(100);
      break;
  }
  delay(10);
}

// =========================================================
// ==================== LÓGICA =============================
// =========================================================

void loopIdle() {
  mostrarInfoReposo("Esperando..."); 
  
  int distancia = medirDistancia();
  
  if (distancia > 0 && distancia < DISTANCIA_MAX) {
    if (isTanqueLleno()) return; 

    mostrarInfoReposo("Detectado!");
    
    if (enviarTriggerAServidor()) {
      estadoActual = ESPERANDO_SERVIDOR;
      tiempoInicioEspera = millis();
    } else {
      mostrarInfoReposo("Error Server");
      delay(2000);
    }
  }
}

void loopEsperandoRespuesta() {
  if (millis() - tiempoInicioEspera > TIMEOUT_SERVIDOR) {
    mostrarInfoReposo("Timeout");
    delay(2000);
    estadoActual = IDLE;
    return;
  }

  static unsigned long ultimaConsulta = 0;
  if (millis() - ultimaConsulta > 1000) {
    ultimaConsulta = millis();
    mostrarInfoReposo("Analizando...");
    
    String comando = consultarComandoServidor();

    if (comando == "PERRO") {
      ultimaMascota = "Perro";
      procesarDispensado(5000, "PERRO"); 
    } 
    else if (comando == "GATO") {
      ultimaMascota = "Gato";
      procesarDispensado(3000, "GATO"); 
    }
    else if (comando == "DESCONOCIDO") {
       mostrarInfoReposo("No Reconocido");
       delay(2000);
       estadoActual = IDLE;
    }
  }
}

// =========================================================
// ==================== ACCIONES FÍSICAS ===================
// =========================================================

void gestionarErrorNivel() {
  digitalWrite(PIN_RELE_BOMBA, HIGH); // Apagar
  
  dibujarEncabezado();
  
  // Alerta Visual
  display.fillRect(0, 15, 128, 49, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(2);
  display.setCursor(30, 25);
  display.println("STOP!");
  display.setTextSize(1);
  display.setCursor(20, 45);
  display.println("TANQUE LLENO");
  display.display();

  // AQUÍ ESTÁ EL CAMBIO:
  // Llamamos a la función unificada. Ella sola controlará el tiempo (2s)
  // para no saturar, pero enviará el estado "LLENO" en la URL.
  enviarDatosUnificados(false);
  
  delay(100); 
}

void procesarDispensado(int duracionMs, String tipoAnimal) {
  estadoActual = DISPENSANDO;
  
  if (isTanqueLleno()) return;

  digitalWrite(PIN_RELE_BOMBA, LOW); // ON

  int pasosTotales = duracionMs / 100;
  float mlActuales = 0;
  
  for (int i = 0; i < pasosTotales; i++) {
    if (isTanqueLleno()) {
       digitalWrite(PIN_RELE_BOMBA, HIGH);
       gestionarErrorNivel(); 
       estadoActual = COOLDOWN_STATE;
       return; 
    }

    mlActuales += (float)ML_POR_SEGUNDO / 10.0;
    
    dibujarEncabezado();
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.println(tipoAnimal);
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Sirviendo: "); 
    display.print((int)mlActuales); display.println("mL");
    display.display();
    
    delay(100); 
  }

  digitalWrite(PIN_RELE_BOMBA, HIGH); // OFF
  logSerial("ACCION", "Fin dispensado");
  
  dibujarEncabezado();
  display.setCursor(20, 30); display.setTextSize(2); display.println("LISTO!");
  display.display();
  delay(2000);
  
  estadoActual = COOLDOWN_STATE;
}

// =========================================================
// ==================== AUXILIARES =========================
// =========================================================

bool enviarTriggerAServidor() {
  HTTPClient http;
  String url = "http://" + serverIP + ":" + String(serverPort) + "/trigger_detection";
  logSerial("HTTP", "GET " + url); 
  http.begin(url); int c = http.GET(); http.end();
  return (c == 200);
}

String consultarComandoServidor() {
  HTTPClient http;
  String url = "http://" + serverIP + ":" + String(serverPort) + "/check_command";
  http.begin(url); int c = http.GET();
  String p = "ERROR";
  if (c == 200) { p = http.getString(); p.trim(); p.replace("\"", ""); }
  http.end();
  return p;
}

int medirDistancia() {
  digitalWrite(PIN_TRIGGER, LOW); delayMicroseconds(2);
  digitalWrite(PIN_TRIGGER, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIGGER, LOW);
  long t = pulseIn(PIN_ECHO, HIGH, 30000);
  return (t == 0) ? -1 : (t / 2) / 29.1;
}

void actualizarTemperatura() {
  static unsigned long lastTime = 0;
  if (millis() - lastTime < 1000) return;
  lastTime = millis();
  sensorTemp.requestTemperatures(); 
  float t = sensorTemp.getTempCByIndex(0);
  if (t > -50 && t != 85.0 && t != DEVICE_DISCONNECTED_C) temperaturaAgua = t;
}

void calibrarLuzAutomaticamente() {
  static unsigned long lastCalib = 0;
  if (millis() - lastCalib < 10000) return;
  lastCalib = millis();
  int val = analogRead(PIN_LDR);
  if (val > luzMax) luzMax = val; if (val < luzMin) luzMin = val;
}

void controlarLuces() {
  bool oscuro = detectarOscuridad();
  if (oscuro && !lucesEncendidas) {
    digitalWrite(PIN_RELE_LUCES, LOW); lucesEncendidas = true;
    mostrarInfoReposo("Oscuro"); 
    
    // Forzamos envío inmediato para que el servidor sepa de las luces YA
    enviarDatosUnificados(true); 
  } 
  else if (!oscuro && lucesEncendidas) {
    digitalWrite(PIN_RELE_LUCES, HIGH); lucesEncendidas = false;
    mostrarInfoReposo("Claro"); 
    
    // Forzamos envío inmediatos
    enviarDatosUnificados(true);
  }
}

bool detectarOscuridad() {
  int valor = analogRead(PIN_LDR);
  int umbral = luzMin + (luzMax - luzMin) * 0.7; 
  if (luzMax - luzMin < 100) umbral = 2000;
  return valor > umbral;
}