/*
  Arduino Nano - Control de riego por humedad de suelo
  Programable por Serial y persistente en EEPROM

  Sensor: A1
  Salida: D2

  Comandos por Serial (sin unidades):
    set=30        -> humedad %
    riego=60      -> segundos
    bloqueo=7200  -> segundos
*/

#include <EEPROM.h>

#define SENSOR_PIN A1
#define SALIDA_PIN 2

// ===== EEPROM MAP =====
#define EEPROM_SETPOINT   0     // uint8_t
#define EEPROM_RIEGO_MS   1     // uint32_t (1..4)
#define EEPROM_BLOQUEO_MS 5     // uint32_t (5..8)

// ===== Defaults =====
#define SETPOINT_DEFAULT        35
#define RIEGO_DEFAULT_MS        60000UL
#define BLOQUEO_DEFAULT_MS      7200000UL

// ===== Calibración sensor =====
#define ADC_HUMEDO 275
#define ADC_SECO   615

int setpoint;
uint32_t tiempoRiegoMs;
uint32_t tiempoBloqueoMs;

String inBuf;

// ===== Estados =====
enum Estado {
  IDLE,
  RIEGO_ACTIVO,
  BLOQUEO
};

Estado estado = IDLE;
unsigned long tEstado = 0;

// =====================================================
// Humedad (%)
// =====================================================
static int humedadPorcentaje(int adc)
{
  if (adc <= ADC_HUMEDO) return 100;
  if (adc >= ADC_SECO)   return 0;

  long hum = (long)(ADC_SECO - adc) * 100L
           / (long)(ADC_SECO - ADC_HUMEDO);
  return (int)hum;
}

// =====================================================
// EEPROM helpers
// =====================================================
static void eepromReadU32(int addr, uint32_t &v)
{
  EEPROM.get(addr, v);
}

static void eepromUpdateU32(int addr, uint32_t v)
{
  uint32_t cur;
  EEPROM.get(addr, cur);
  if (cur != v) EEPROM.put(addr, v);
}

// =====================================================
// Cargar configuración desde EEPROM
// =====================================================
static void loadConfig()
{
  uint8_t sp = EEPROM.read(EEPROM_SETPOINT);
  setpoint = (sp <= 100) ? sp : SETPOINT_DEFAULT;

  eepromReadU32(EEPROM_RIEGO_MS, tiempoRiegoMs);
  if (tiempoRiegoMs < 1000 || tiempoRiegoMs > 3600000UL)
    tiempoRiegoMs = RIEGO_DEFAULT_MS;

  eepromReadU32(EEPROM_BLOQUEO_MS, tiempoBloqueoMs);
  if (tiempoBloqueoMs < 1000 || tiempoBloqueoMs > 86400000UL)
    tiempoBloqueoMs = BLOQUEO_DEFAULT_MS;
}

// =====================================================
// Guardar configuración
// =====================================================
static void saveSetpoint(int v)
{
  EEPROM.update(EEPROM_SETPOINT, v);
}

static void saveRiego(uint32_t v)
{
  eepromUpdateU32(EEPROM_RIEGO_MS, v);
}

static void saveBloqueo(uint32_t v)
{
  eepromUpdateU32(EEPROM_BLOQUEO_MS, v);
}

// =====================================================
// Procesar comando Serial
// =====================================================
static void processCommand(String s)
{
  s.trim();
  s.toLowerCase();

  int eq = s.indexOf('=');
  if (eq < 0) return;

  String key = s.substring(0, eq);
  String val = s.substring(eq + 1);

  long num = val.toInt();

  if (key == "set") {
    num = constrain(num, 0, 100);
    if (num != setpoint) {
      setpoint = num;
      saveSetpoint(setpoint);
    }
    Serial.print("Setpoint = ");
    Serial.print(setpoint);
    Serial.println("%");
  }

  else if (key == "riego") {
    if (num > 0) {
      tiempoRiegoMs = (uint32_t)num * 1000UL;
      saveRiego(tiempoRiegoMs);
      Serial.print("Riego = ");
      Serial.print(num);
      Serial.println(" s");
    }
  }

  else if (key == "bloqueo") {
    if (num > 0) {
      tiempoBloqueoMs = (uint32_t)num * 1000UL;
      saveBloqueo(tiempoBloqueoMs);
      Serial.print("Bloqueo = ");
      Serial.print(num);
      Serial.println(" s");
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  pinMode(SALIDA_PIN, OUTPUT);
  digitalWrite(SALIDA_PIN, LOW);

  Serial.begin(115200);
  loadConfig();

  Serial.println("Sistema de riego listo");
  Serial.print("Setpoint: "); Serial.print(setpoint); Serial.println("%");
  Serial.print("Riego: "); Serial.print(tiempoRiegoMs / 1000); Serial.println(" s");
  Serial.print("Bloqueo: "); Serial.print(tiempoBloqueoMs / 1000); Serial.println(" s");
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  unsigned long ahora = millis();

  // ---- Serial ----
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inBuf.length()) {
        processCommand(inBuf);
        inBuf = "";
      }
    } else {
      if (inBuf.length() < 32) inBuf += c;
    }
  }

  // ---- Sensor ----
  int adc = analogRead(SENSOR_PIN);
  int humedad = humedadPorcentaje(adc);

  // ---- Estados ----
  switch (estado) {

    case IDLE:
      digitalWrite(SALIDA_PIN, LOW);
      if (humedad < setpoint) {
        estado = RIEGO_ACTIVO;
        tEstado = ahora;
        digitalWrite(SALIDA_PIN, HIGH);
      }
      break;

    case RIEGO_ACTIVO:
      if (ahora - tEstado >= tiempoRiegoMs) {
        digitalWrite(SALIDA_PIN, LOW);
        estado = BLOQUEO;
        tEstado = ahora;
      }
      break;

    case BLOQUEO:
      digitalWrite(SALIDA_PIN, LOW);
      if (ahora - tEstado >= tiempoBloqueoMs) {
        estado = IDLE;
      }
      break;
  }

  // ---- Serial Plotter ----
  Serial.print("Top:100 ");
  Serial.print("Bottom:0 ");
  Serial.print("Set:");
  Serial.print(setpoint);
  Serial.print(" ");
  Serial.print("Hum:");
  Serial.print(humedad);
  Serial.println();

  delay(200);
}
