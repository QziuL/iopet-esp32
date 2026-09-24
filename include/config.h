#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// Configurações de Rede Wi-Fi
// =============================================================================
#define WIFI_SSID           "INTELBRAS"
#define WIFI_PASSWORD       "lrs123456"

// =============================================================================
// Configurações do Broker RabbitMQ (Interface MQTT)
// =============================================================================
// IMPORTANTE: Insira o endereço IP da máquina local ou servidor onde o RabbitMQ está rodando.
// Não utilize "localhost" ou "127.0.0.1", pois o ESP32 é um dispositivo externo na rede.
#define MQTT_BROKER_HOST    "10.0.0.138"
#define MQTT_BROKER_PORT    1883
#define MQTT_USER           "guest"
#define MQTT_PASSWORD       "guest"

// Tópico MQTT para envio da telemetria.
// No RabbitMQ com o plugin rabbitmq_mqtt ativado, o tópico "iopet/telemetria"
// é mapeado automaticamente para o TopicExchange "amq.topic" com routing key "iopet.telemetria".
// Como o backend Spring Boot escuta com routing key "iopet.telemetria.#",
// a mensagem é roteada diretamente para a fila "iopet.telemetria.queue".
#define MQTT_TOPIC_TELEMETRIA "iopet/telemetria"

// =============================================================================
// Configurações de Hardware e Pinos (ESP32-C3)
// =============================================================================
#define GPS_RX_PIN          5   // Pino RX do ESP32 conectado ao TX do GPS
#define GPS_TX_PIN          4   // Pino TX do ESP32 conectado ao RX do GPS
#define GPS_BAUD_RATE       9600
#define LED_PIN             LED_BUILTIN

// =============================================================================
// Intervalos de Operação (em milissegundos)
// =============================================================================
#define INTERVALO_TELEMETRIA_MS  5000   // Enviar dados a cada 5 segundos
#define INTERVALO_PISCA_LED_MS   500    // Piscar LED de status

// =============================================================================
// Configurações de Telemetria e Bateria
// =============================================================================
// Como o protótipo ainda não possui medição física de bateria por divisor de tensão,
// utiliza-se um valor fixo percentual (ex: 100% de bateria).
#define VALOR_BATERIA_FIXO       100

// Modo de simulação para testes de bancada em ambientes fechados (indoor):
// Em ambientes fechados, o módulo GPS NEO-6M pode demorar muito ou não obter fix.
// Se definido como true, caso o GPS não possua coordenadas válidas no momento do envio,
// o dispositivo enviará as coordenadas simuladas abaixo para testar a comunicação com o RabbitMQ/Backend.
// Para operação real em campo, defina como false.
#define SIMULAR_GPS_SE_SEM_FIX   false
#define LATITUDE_SIMULADA        -25.542123
#define LONGITUDE_SIMULADA       -46.534567

#endif // CONFIG_H
