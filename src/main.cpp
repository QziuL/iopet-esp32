#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>
#include "config.h"

// =============================================================================
// Objetos Globais
// =============================================================================
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =============================================================================
// Variáveis de Controle e Temporização
// =============================================================================
unsigned long ultimaTrocaLed = 0;
unsigned long ultimoEnvioTelemetria = 0;
unsigned long ultimaTentativaWifi = 0;
unsigned long ultimaTentativaMqtt = 0;
bool estadoLed = false;
String macAddressStr = "";

// =============================================================================
// Protótipos de Funções
// =============================================================================
void conectarWifi();
void escanearRedesWifi();
void verificarConexaoWifi();
void conectarMqtt();
void enviarTelemetria(double latitude, double longitude, int bateria);
void atualizarLed();
const char* descreverRazaoDesconexao(uint8_t reason);
const char* descreverErroMqtt(int state);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("         🐾 IoPet - Rastreador IoT GPS 🐾        ");
  Serial.println("==================================================");

  // Inicializa comunicação serial com o módulo GPS NEO-6M
  gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("GPS Serial iniciado (RX=%d, TX=%d, Baud=%d)\n", GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_RATE);

  // Registra ouvinte de eventos do subsistema Wi-Fi
  WiFi.onEvent(onWiFiEvent);

  // Inicializa Wi-Fi em modo Station e limpa sessões residuais
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);

  macAddressStr = WiFi.macAddress();
  Serial.printf("Identificador único do dispositivo (MAC): %s\n", macAddressStr.c_str());

  // Varredura de diagnóstico para verificar se o rádio enxerga a rede 2.4 GHz
  escanearRedesWifi();

  // Conecta ao Wi-Fi
  conectarWifi();

  // Configuração do Cliente MQTT
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setBufferSize(512); // Buffer para JSON
  Serial.printf("Configurado Broker RabbitMQ MQTT: %s:%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
}

void loop() {
  // Leitura contínua dos dados NMEA transmitidos pelo GPS
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Manutenção das conexões
  verificarConexaoWifi();
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      conectarMqtt();
    } else {
      mqttClient.loop();
    }
  }

  // Pisca LED para indicar atividade do microcontrolador
  atualizarLed();

  // Ciclo periódico de envio de telemetria
  const unsigned long agora = millis();
  if (agora - ultimoEnvioTelemetria >= INTERVALO_TELEMETRIA_MS) {
    ultimoEnvioTelemetria = agora;

    bool gpsValido = gps.location.isValid() && (gps.location.age() < 10000);

    if (gpsValido) {
      double lat = gps.location.lat();
      double lng = gps.location.lng();
      Serial.printf("[GPS] Fix OK! Satélites: %u, Lat: %.6f, Lng: %.6f, Precisão (HDOP): %.2f\n",
                    gps.satellites.value(), lat, lng, gps.hdop.hdop());
      enviarTelemetria(lat, lng, VALOR_BATERIA_FIXO);
    } else if (SIMULAR_GPS_SE_SEM_FIX) {
      Serial.printf("[GPS] Sem fix real (Satélites: %u, Caracteres: %u). Enviando coordenadas de simulação.\n",
                    gps.satellites.value(), gps.charsProcessed());
      enviarTelemetria(LATITUDE_SIMULADA, LONGITUDE_SIMULADA, VALOR_BATERIA_FIXO);
    } else {
      Serial.printf("[GPS] Aguardando fix dos satélites... Satélites visíveis: %u | Caracteres recebidos: %u\n",
                    gps.satellites.value(), gps.charsProcessed());
    }
  }
}

// =============================================================================
// Funções de Rede e Conectividade
// =============================================================================

void escanearRedesWifi() {
  Serial.println("\n[WiFi Scan] Escaneando redes Wi-Fi 2.4 GHz disponíveis...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("[WiFi Scan] ❌ Nenhuma rede encontrada no alcance!");
    Serial.println("[WiFi Scan] Dica: verifique se a antena do ESP32 está desobstruída e se a alimentação USB está estável.");
  } else {
    Serial.printf("[WiFi Scan] %d redes 2.4 GHz encontradas:\n", n);
    bool redeConfiguradaVisivel = false;
    for (int i = 0; i < n; ++i) {
      String ssidAtual = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      Serial.printf("   [%2d] %-25s | Sinal: %3d dBm | Canal: %2d\n",
                    i + 1, ssidAtual.c_str(), rssi, WiFi.channel(i));
      if (ssidAtual.equalsIgnoreCase(WIFI_SSID)) {
        redeConfiguradaVisivel = true;
      }
    }
    if (redeConfiguradaVisivel) {
      Serial.printf("[WiFi Scan] ✅ A rede configurada '%s' foi localizada pelo rádio!\n", WIFI_SSID);
    } else {
      Serial.printf("[WiFi Scan] ⚠️ ATENÇÃO: A rede configurada '%s' NÃO foi encontrada pelo rádio 2.4 GHz!\n", WIFI_SSID);
      Serial.println("             O ESP32-C3 NÃO enxerga redes de 5 GHz. Se estiver usando Hotspot do celular,");
      Serial.println("             ative 'Maximizar Compatibilidade' (iPhone) ou 'Banda do AP: 2.4 GHz' (Android).");
    }
  }
  Serial.println("--------------------------------------------------\n");
}

void conectarWifi() {
  // Ajusta a potência de transmissão de RF para reduzir picos de corrente no regulador USB
  WiFi.setTxPower(WIFI_POWER_15dBm);

  Serial.printf("[WiFi] Conectando a rede: '%s' ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicio < 15000)) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] ✅ Conectado com sucesso! Endereço IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] ❌ Falha na conexão inicial (timeout de 15s). Tentativas em segundo plano continuarão.");
  }
}

void verificarConexaoWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long agora = millis();
    if (agora - ultimaTentativaWifi >= 10000) {
      ultimaTentativaWifi = agora;
      Serial.printf("[WiFi] Reconectando à rede '%s'...\n", WIFI_SSID);
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi Event] Interface Station pronta.");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi Event] Conectado ao Access Point! Aguardando atribuição de IP DHCP...");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi Event] IP obtido com sucesso: %s\n",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t motivo = info.wifi_sta_disconnected.reason;
      Serial.printf("[WiFi Event] Desconectado. Motivo código %d: %s\n",
                    motivo, descreverRazaoDesconexao(motivo));
      break;
    }
    default:
      break;
  }
}

const char* descreverRazaoDesconexao(uint8_t reason) {
  switch (reason) {
    case 1:   return "UNSPECIFIED (Não especificado)";
    case 2:   return "AUTH_EXPIRE (Autenticação expirada)";
    case 3:   return "AUTH_LEAVE (Dispositivo solicitou desconexão)";
    case 4:   return "ASSOC_EXPIRE (Associação expirou)";
    case 5:   return "ASSOC_TOOMANY (Muitos dispositivos conectados ao AP)";
    case 6:   return "NOT_AUTHED (Não autenticado)";
    case 7:   return "NOT_ASSOCED (Não associado)";
    case 8:   return "ASSOC_LEAVE (Associação encerrada)";
    case 9:   return "ASSOC_NOT_AUTHED (Associação sem autenticação prévia)";
    case 14:  return "MIC_FAILURE (Falha de integridade da senha)";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT (Timeout de criptografia WPA. Verifique a senha!)";
    case 201: return "NO_AP_FOUND (Rede não localizada! Verifique o SSID ou se a rede é 2.4 GHz)";
    case 202: return "AUTH_FAIL (Falha de autenticação! Senha incorreta)";
    case 203: return "ASSOC_FAIL (Falha de associação com o roteador)";
    case 204: return "HANDSHAKE_TIMEOUT (Timeout no aperto de mão da segurança)";
    case 205: return "CONNECTION_FAIL (Falha geral de conexão)";
    default:  return "ERRO DE CONEXÃO";
  }
}

const char* descreverErroMqtt(int state) {
  switch (state) {
    case -4: return "TIMEOUT (Broker não respondeu no IP/porta configurado)";
    case -3: return "CONNECTION_LOST (Conexão TCP perdida)";
    case -2: return "CONNECT_FAILED (Falha TCP. Verifique IP, porta 1883, plugin MQTT ou firewall)";
    case -1: return "DISCONNECTED (Desconectado)";
    case 1:  return "BAD_PROTOCOL (Versão incompatível do protocolo MQTT)";
    case 2:  return "BAD_CLIENT_ID (Client ID rejeitado pelo broker)";
    case 3:  return "UNAVAILABLE (Broker indisponível)";
    case 4:  return "BAD_CREDENTIALS (Usuário ou senha incorretos)";
    case 5:  return "UNAUTHORIZED (Acesso não autorizado)";
    default: return "DESCONHECIDO";
  }
}

void conectarMqtt() {
  unsigned long agora = millis();
  if (agora - ultimaTentativaMqtt < 5000) {
    return; // Evita tentativas frequentes
  }
  ultimaTentativaMqtt = agora;

  if (mqttClient.connected()) {
    return;
  }

  String clientId = "IoPet-" + WiFi.macAddress();
  clientId.replace(":", "");

  Serial.printf("[MQTT] Conectando ao RabbitMQ (%s:%d) com ClientID '%s'...\n",
                MQTT_BROKER_HOST, MQTT_BROKER_PORT, clientId.c_str());

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.println("[MQTT] Conexão estabelecida com sucesso!");
  } else {
    int erro = mqttClient.state();
    Serial.printf("[MQTT] Falha na conexão. Código: %d -> %s. Próxima tentativa em 5s.\n",
                  erro, descreverErroMqtt(erro));
  }
}

// =============================================================================
// Envio da Telemetria para o RabbitMQ
// =============================================================================

void enviarTelemetria(double latitude, double longitude, int bateria) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TELEMETRIA] Mensagem retida: Wi-Fi não conectado.");
    return;
  }

  if (!mqttClient.connected()) {
    int erro = mqttClient.state();
    Serial.printf("[TELEMETRIA] Mensagem retida: broker MQTT não conectado (Estado: %d -> %s).\n",
                  erro, descreverErroMqtt(erro));
    return;
  }

  JsonDocument doc;
  doc["device_id"] = macAddressStr;
  doc["latitude"]  = latitude;
  doc["longitude"] = longitude;
  doc["battery"]   = bateria;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

  Serial.printf("[TELEMETRIA] Publicando no tópico '%s':\n%s\n", MQTT_TOPIC_TELEMETRIA, jsonBuffer);

  bool publicado = mqttClient.publish(MQTT_TOPIC_TELEMETRIA, jsonBuffer);

  if (publicado) {
    Serial.println("[TELEMETRIA] Publicado com sucesso no RabbitMQ!");
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
  } else {
    Serial.println("[TELEMETRIA] Falha ao publicar mensagem no broker.");
  }
}

void atualizarLed() {
  const unsigned long agora = millis();
  if (agora - ultimaTrocaLed >= INTERVALO_PISCA_LED_MS) {
    ultimaTrocaLed = agora;
    estadoLed = !estadoLed;
    digitalWrite(LED_PIN, estadoLed ? HIGH : LOW);
  }
}