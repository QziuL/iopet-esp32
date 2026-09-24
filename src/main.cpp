#include <Arduino.h>
#include <TinyGPSPlus.h>

const unsigned long intervaloLed = 500;
const unsigned long intervaloGps = 2000;
const int gpsRxPin = 5;
const int gpsTxPin = 4;
unsigned long ultimaTroca = 0;
unsigned long ultimoRelatorioGps = 0;
bool estadoLed = false;
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, gpsRxPin, gpsTxPin);
  delay(500);
  Serial.println("ESP32-C3 Super Mini iniciado");
  Serial.println("GPS NEO-6M: aguardando dados...");
}

void loop() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  const unsigned long agora = millis();
  if (agora - ultimaTroca >= intervaloLed) {
    ultimaTroca = agora;
    estadoLed = !estadoLed;
    digitalWrite(LED_BUILTIN, estadoLed ? HIGH : LOW);
  }

  if (agora - ultimoRelatorioGps < intervaloGps) {
    return;
  }

  ultimoRelatorioGps = agora;
  if (gps.location.isValid()) {
    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);
    Serial.print("Satellites: ");
    Serial.println(gps.satellites.value());
  } else {
    Serial.print("GPS sem fix. Caracteres recebidos: ");
    Serial.println(gps.charsProcessed());
  }
}