#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h> // NOVO: Biblioteca para salvar dados na memória Flash

#define SDA_PIN 21
#define SCL_PIN 22
constexpr int LED_PIN = 2;
constexpr unsigned long DEEP_SLEEP_INTERVAL_MS = 180000; // 3 minutos

constexpr float TEMP_MIN = 22.0;
constexpr float TEMP_MAX = 36.0;
constexpr float HUM_MIN = 50.0;
constexpr float HUM_MAX = 90.0;

// Constantes do MQTT (hardcoded para simplificar)
const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_DADOS = "colmeia/dados";
const char* MQTT_TOPIC_ALERTA = "colmeia/alerta";
const char* MQTT_CLIENT_ID_PREFIX = "ColmeiaESP-";

// NOVO: Objeto de Preferências
Preferences preferences; 

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_AHTX0 aht;
String deviceId;
char nome_colmeia[40] = "Colmeia 01";
char conta_usuario[40] = "usuario@email.com";

// =================================================================
// FUNÇÕES DE PERSISTÊNCIA (PREFERENCES)
// =================================================================

// Carrega os dados salvos da memória Flash para as variáveis globais
void carregaConfiguracao() {
  preferences.begin("colmeia-config", true); // "colmeia-config" é o namespace (read-only)
  
  // Se existir um valor salvo, carrega. Senão, usa o valor padrão da variável global.
  String nomeSalvo = preferences.getString("nome", nome_colmeia);
  String contaSalva = preferences.getString("conta", conta_usuario);

  // Copia os valores da String para o array char[]
  nomeSalvo.toCharArray(nome_colmeia, 40);
  contaSalva.toCharArray(conta_usuario, 40);

  preferences.end();
  Serial.printf("✅ Configuração carregada. Nome: %s | Conta: %s\n", nome_colmeia, conta_usuario);
}

// Salva as variáveis globais na memória Flash
void salvaConfiguracao() {
  preferences.begin("colmeia-config", false); // Modo de escrita
  
  preferences.putString("nome", nome_colmeia);
  preferences.putString("conta", conta_usuario);

  preferences.end();
  Serial.println("✅ Configuração salva na Flash.");
}

// =================================================================
// FUNÇÕES DO SENSOR
// =================================================================

bool iniciaSensor() {
  Wire.begin(SDA_PIN, SCL_PIN);
  return aht.begin(&Wire);
}

bool leSensor(float &temperatura, float &umidade) {
  sensors_event_t hum_event, temp_event;
  aht.getEvent(&hum_event, &temp_event);
  if (isnan(temp_event.temperature) || isnan(hum_event.relative_humidity)) {
    return false;
  }
  temperatura = temp_event.temperature;
  umidade = hum_event.relative_humidity;
  return true;
}

String getDeviceId() {
  return WiFi.macAddress();
}

// =================================================================
// FUNÇÕES DE REDE
// =================================================================

void conectaWiFi() {
  WiFiManager wm;
  wm.setTitle("Configurar Colmeia");

  // Cria parâmetros customizados com os valores atuais carregados da Flash
  WiFiManagerParameter custom_nome_colmeia("nome", "Nome da Colmeia", nome_colmeia, 40);
  WiFiManagerParameter custom_conta_usuario("conta", "Conta do Usuário", conta_usuario, 40);

  wm.addParameter(&custom_nome_colmeia);
  wm.addParameter(&custom_conta_usuario);

  // Define um timeout mais curto para o portal de configuração para economizar bateria.
  wm.setTimeout(60); 

  if (!wm.autoConnect("ColmeiaSetup")) {
    Serial.println("❌ Portal de configuração expirou. Indo dormir para tentar mais tarde.");
    return; // Simplesmente retorna se não conseguir conectar.
  }

  // Copia os valores configurados (se houver) para as variáveis globais
  strcpy(nome_colmeia, custom_nome_colmeia.getValue());
  strcpy(conta_usuario, custom_conta_usuario.getValue());
  
  // NOVO: Salva os novos valores na memória Flash
  salvaConfiguracao(); 

  Serial.print("\n✅ WiFi conectado! IP: ");
  Serial.println(WiFi.localIP());
}

void conectaMQTT() {
  client.setServer(MQTT_SERVER, MQTT_PORT);
  String clientId = String(MQTT_CLIENT_ID_PREFIX) + deviceId;

  int tentativas = 0;
  while (!client.connected() && tentativas < 3) {
    Serial.printf("Tentando conectar ao MQTT (tentativa %d/3)...\n", tentativas + 1);

    if (client.connect(clientId.c_str())) {
      Serial.println("✅ Conectado ao MQTT!");
      return;
    } else {
      Serial.printf("❌ Falha na conexão MQTT, rc=%d. Tentando novamente em 1 segundo.\n", client.state());
      delay(1000);
      tentativas++;
    }
  }
  Serial.println("❌ Não foi possível conectar ao MQTT após várias tentativas.");
}

// =================================================================
// FUNÇÕES DE UTILIDADE
// =================================================================

void piscaLed(int vezes, int duracao) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duracao);
    digitalWrite(LED_PIN, LOW);
    delay(duracao);
  }
}

// =================================================================
// SETUP E LOOP
// =================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=========================");
  Serial.println("   INICIANDO SENSOR     ");
  Serial.println("=========================");
  
  // NOVO: Carrega as configurações salvas da Flash
  carregaConfiguracao(); 

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!iniciaSensor()) {
    Serial.println("❌ Erro ao iniciar sensor AHT10! Reiniciando...");
    delay(3000);
    ESP.restart();
  }
  Serial.println("✅ Sensor AHT10 iniciado.");

  deviceId = getDeviceId();
  Serial.println("ID do dispositivo: " + deviceId);

  float temperatura, umidade;
  if (!leSensor(temperatura, umidade)) {
    Serial.println("⚠️ Falha ao ler o sensor. Entrando em sleep.");
  } else {
    temperatura = round(temperatura * 10) / 10.0;
    umidade = round(umidade * 10) / 10.0;
    bool isAlerta = (temperatura < TEMP_MIN || temperatura > TEMP_MAX || umidade < HUM_MIN || umidade > HUM_MAX);

    Serial.printf("🌡️ %.1f°C | 💧 %.1f%% | Alerta: %s\n", temperatura, umidade, isAlerta ? "SIM" : "NÃO");

    digitalWrite(LED_PIN, HIGH);
    conectaWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      conectaMQTT();
      if (client.connected()) {
        DynamicJsonDocument payload_doc(256);
        payload_doc["h"] = umidade;
        payload_doc["t"] = temperatura;
        payload_doc["id"] = deviceId;
        payload_doc["nome_colmeia"] = nome_colmeia;
        payload_doc["conta_usuario"] = conta_usuario;

        String payload_str;
        serializeJson(payload_doc, payload_str);

        const char* topic = isAlerta ? MQTT_TOPIC_ALERTA : MQTT_TOPIC_DADOS;
        
        client.publish(topic, payload_str.c_str());
        Serial.println("✅ Dados enviados via MQTT!");
      } else {
        Serial.println("⚠️ Falha ao conectar no MQTT. Verifique o broker.");
        piscaLed(2, 500); // 2 piscadas lentas para erro de MQTT
      }
    } else {
      Serial.println("⚠️ Sem conexão WiFi, não foi possível enviar os dados.");
      piscaLed(3, 150); // 3 piscadas rápidas para erro de WiFi
    }
    digitalWrite(LED_PIN, LOW);
  }

  Serial.printf("💤 Entrando em Deep Sleep por %lu segundos...\n\n", DEEP_SLEEP_INTERVAL_MS / 1000);
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_INTERVAL_MS * 1000); // Converte milissegundos para microssegundos
  esp_deep_sleep_start();
}

void loop() {
  // Loop vazio, o dispositivo está em Deep Sleep e nunca chega aqui.
}