// =================================================================
// ==                 SENSORES-MITESCAN (ESP32/AHT10)               ==
// =================================================================
// Este código é otimizado para um ESP32 usando o sensor AHT10.
//
// Funcionalidades:
// - Lê dados de temperatura e umidade do sensor AHT10.
// - Permite configurar WiFi e credenciais MQTT via portal cativo (WiFiManager).
// - Salva leituras localmente no sistema de arquivos (LittleFS).
// - Envia dados para um broker MQTT em intervalos ou quando um alerta é gerado.
// - Implementa modo de economia de energia (Modem Sleep) para o Wi-Fi.
// - Gera um ID único para o dispositivo baseado no endereço MAC.
// =================================================================
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ==== CONFIGURAÇÕES ====
#define SDA_PIN 21
#define SCL_PIN 22
constexpr int LED_PIN = 2;
constexpr unsigned long INTERVALO_LEITURA = 15000; // 15 segundos
constexpr int CICLOS_ENVIO = 4;                     // Envia a cada 4 leituras (~1 min)

// Limites para geração de alertas
constexpr float TEMP_MIN = 22.0;
constexpr float TEMP_MAX = 36.0;
constexpr float HUM_MIN = 50.0;
constexpr float HUM_MAX = 90.0;

// ==== MQTT ====
char mqtt_server[40] = "192.168.3.119";
char mqtt_port[6] = "1883";
char mqtt_user[40] = "eder";
char mqtt_pass[40] = "310104";
const char* MQTT_TOPIC_DADOS = "colmeia/dados";
const char* MQTT_TOPIC_ALERTA = "colmeia/alerta";
const char* MQTT_CLIENT_ID_PREFIX = "ColmeiaESP-";

// ==== OBJETOS ====
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_AHTX0 aht;
String uid;

// ==== VARIÁVEIS ====
int leituras_count = 0;

// ==== FUNÇÕES ====

// ---- Funções de Sensor Abstraídas ----
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

// ---- Funções de Armazenamento Local ----
void salvaDadoLocal(float t, float h, bool alerta) {
  if (!LittleFS.begin()) {
      Serial.println("❌ Falha ao montar LittleFS para salvar.");
      return;
  }

  DynamicJsonDocument doc(4096);
  DynamicJsonDocument doc(4096); // Aumente se esperar muitos dados acumulados
  File file = LittleFS.open("/dados.json", "r");
  
  if (file) {
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
      Serial.print("❌ Falha ao ler dados.json, recriando arquivo. Erro: ");
      Serial.println(error.c_str());
      doc.clear(); // Limpa o documento para começar um novo array
    }
  }

  JsonArray arr;
  if (doc.is<JsonArray>()) {
    arr = doc.as<JsonArray>();
  } else {
    // Se o doc não for um array (vazio ou corrompido), cria um novo
    arr = doc.to<JsonArray>();
  }

  JsonObject o = arr.createNestedObject();
  o["t"] = t;
  o["h"] = h;
  o["a"] = alerta;
  o["ts"] = millis();

  file = LittleFS.open("/dados.json", "w");
  serializeJson(doc, file);
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("💾 Dado salvo no LittleFS");
  } else {
    Serial.println("❌ Falha ao abrir dados.json para escrita.");
  }
}

// ---- Funções de Rede ----
String getUID_from_MAC() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[17];
  snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)mac);
  return String(buf);
}

void conectaWiFi() {
  WiFiManager wm;
  wm.setTitle("Configurar Colmeia");

  // Parâmetros customizados para o portal
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Pass", mqtt_pass, 40);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);

  wm.setTimeout(180); // Timeout do portal: 3 minutos
  bool ok = wm.autoConnect("ColmeiaSetup");
  if (!ok) {
    Serial.println("❌ Falha WiFi, reiniciando...");
    ESP.restart();
  }

  // Salva os valores configurados no portal
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());

  Serial.print("\n✅ WiFi conectado! IP: ");
  Serial.println(WiFi.localIP());
}

void conectaMQTT() {
  client.setServer(mqtt_server, atoi(mqtt_port));
  String clientId = String(MQTT_CLIENT_ID_PREFIX) + uid;

  int tentativas = 0;
  while (!client.connected() && tentativas < 3) {
    Serial.printf("Tentando conectar ao MQTT (tentativa %d/3)...\n", tentativas + 1);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
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

void enviaMQTT() {
  if (!LittleFS.begin()) {
    Serial.println("❌ Falha ao montar LittleFS para envio.");
    return;
  }
  File file = LittleFS.open("/dados.json", "r");
  if (!file) {
    Serial.println("ℹ️ Nenhum dado local para enviar.");
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.println("❌ Falha ao ler dados.json para envio.");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  Serial.printf("📤 Enviando %d leituras salvas...\n", arr.size());

  for (JsonObject o : arr) {
    DynamicJsonDocument payload_doc(256);
    payload_doc["h"] = o["h"];
    payload_doc["t"] = o["t"];
    payload_doc["ts"] = o["ts"];
    payload_doc["id"] = uid;

    String payload_str;
    serializeJson(payload_doc, payload_str);

    const char* topic = o["a"] ? MQTT_TOPIC_ALERTA : MQTT_TOPIC_DADOS;
    client.publish(topic, payload_str.c_str());
    
    delay(150); // Pequeno delay para não sobrecarregar o broker/rede
  }

  LittleFS.remove("/dados.json"); // Limpa o arquivo após envio
  Serial.println("✅ Dados enviados e arquivo local limpo!");
}

// ---- Funções de Gerenciamento de Energia ----
void wifiSleep() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  Serial.println("📴 Wi-Fi em modo Modem Sleep.");
}

void wifiWake() {
  Serial.print("📡 Ligando Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) { // Timeout de 15s
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if(WiFi.status() == WL_CONNECTED) {
    Serial.print("✅ Wi-Fi reconectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("❌ Falha ao reconectar o Wi-Fi.");
  }
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=========================");
  Serial.println("   INICIANDO SENSOR      ");
  Serial.println("=========================");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!iniciaSensor()) {
    Serial.println("❌ Erro ao iniciar sensor AHT10! Reiniciando...");
    delay(3000);
    ESP.restart();
  }
  Serial.println("✅ Sensor AHT10 iniciado.");
  
  if (!LittleFS.begin()) {
    Serial.println("❌ Falha crítica ao iniciar LittleFS. Verifique a partição.");
  }

  uid = getUID_from_MAC();
  Serial.println("ID do dispositivo: " + uid);

  // Se o WiFi não estiver conectado, abre o portal de configuração.
  // Se já estiver, apenas reconecta.
  if (WiFi.status() != WL_CONNECTED) {
      conectaWiFi();
  }

  wifiSleep();  // Começa com Wi-Fi desligado para economizar energia
}

// ==== LOOP ====
void loop() {
  float temperatura, umidade;
  
  if (!leSensor(temperatura, umidade)) {
    Serial.println("⚠️ Falha ao ler o sensor. Pulando esta leitura.");
    delay(INTERVALO_LEITURA);
    return;
  }

  // Arredonda para uma casa decimal
  temperatura = round(temperatura * 10) / 10.0;
  umidade = round(umidade * 10) / 10.0;

  bool alerta = (temperatura < TEMP_MIN || temperatura > TEMP_MAX || umidade < HUM_MIN || umidade > HUM_MAX);

  Serial.printf("🌡️ %.1f°C | 💧 %.1f%% | Alerta: %s\n", temperatura, umidade, alerta ? "SIM" : "NÃO");

  salvaDadoLocal(temperatura, umidade, alerta);

  leituras_count++;
  Serial.printf("📊 Leitura #%d\n", leituras_count);

  // Condições para acordar e enviar dados
  if (alerta || leituras_count >= CICLOS_ENVIO) {
    digitalWrite(LED_PIN, HIGH); // Liga o LED para indicar atividade de rede
    wifiWake();

    if (WiFi.status() == WL_CONNECTED) {
      conectaMQTT();
      if (client.connected()) {
        enviaMQTT();
        leituras_count = 0; // Reseta o contador apenas se o envio for bem-sucedido
      }
    } else {
      Serial.println("⚠️ Sem WiFi! Mantendo dados locais para próxima tentativa.");
      // Pisca o LED para indicar falha de conexão
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH); delay(200);
        digitalWrite(LED_PIN, LOW); delay(200);
      }
    }
    digitalWrite(LED_PIN, LOW);
    wifiSleep();
  }

  Serial.printf("💤 Entrando em modo sleep por %lu segundos...\n\n", INTERVALO_LEITURA / 1000);
  delay(INTERVALO_LEITURA);
}
