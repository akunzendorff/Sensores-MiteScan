#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define SDA_PIN 21
#define SCL_PIN 22
constexpr int LED_PIN = 2;
constexpr unsigned long INTERVALO_LEITURA = 15000;
constexpr int CICLOS_ENVIO = 4;

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

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_AHTX0 aht;
String deviceId;
char nome_colmeia[40] = "Colmeia 01";
char conta_usuario[40] = "usuario@email.com";
int leituras_count = 0;

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

void salvaDadoLocal(float t, float h, bool alerta) {
  File file = LittleFS.open("/dados.jsonl", "a");
  if (file) {
    DynamicJsonDocument doc(128);
    doc["t"] = t;
    doc["h"] = h;
    doc["a"] = alerta;
    doc["ts"] = millis();

    serializeJson(doc, file);
    file.println();
    file.close();
    Serial.println("💾 Dado salvo no LittleFS");
  } else {
    Serial.println("❌ Falha ao abrir arquivo de dados para escrita.");
  }
}

String getDeviceId() {
  return WiFi.macAddress();
}

void conectaWiFi() {
  WiFiManager wm;
  wm.setTitle("Configurar Colmeia");

  WiFiManagerParameter custom_nome_colmeia("nome", "Nome da Colmeia", nome_colmeia, 40);
  WiFiManagerParameter custom_conta_usuario("conta", "Conta do Usuário", conta_usuario, 40);

  wm.addParameter(&custom_nome_colmeia);
  wm.addParameter(&custom_conta_usuario);

  wm.setTimeout(180);
  bool ok = wm.autoConnect("ColmeiaSetup");
  if (!ok) {
    Serial.println("❌ Falha WiFi, reiniciando...");
    ESP.restart();
  }

  strcpy(nome_colmeia, custom_nome_colmeia.getValue());
  strcpy(conta_usuario, custom_conta_usuario.getValue());

  Serial.println("Nome da Colmeia: " + String(nome_colmeia));
  Serial.println("Conta do Usuário: " + String(conta_usuario));

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

void enviaMQTT() {
  File file = LittleFS.open("/dados.jsonl", "r");
  if (!file) {
    Serial.println("ℹ️ Nenhum dado local para enviar.");
    return;
  }

  Serial.println("📤 Enviando leituras salvas...");
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      DynamicJsonDocument doc(128);
      deserializeJson(doc, line);

      DynamicJsonDocument payload_doc(256);
      payload_doc["h"] = doc["h"];
      payload_doc["t"] = doc["t"];
      payload_doc["ts"] = doc["ts"];
      payload_doc["id"] = deviceId;
      payload_doc["nome_colmeia"] = nome_colmeia;
      payload_doc["conta_usuario"] = conta_usuario;

      String payload_str;
      serializeJson(payload_doc, payload_str);

      bool isAlerta = doc["a"];
      const char* topic = isAlerta ? MQTT_TOPIC_ALERTA : MQTT_TOPIC_DADOS;
      
      client.publish(topic, payload_str.c_str());
      delay(100); // Pequeno delay para não sobrecarregar o broker
    }
  }
  file.close();

  LittleFS.remove("/dados.jsonl");
  Serial.println("✅ Dados enviados e arquivo local limpo!");
}

void wifiSleep() {
  WiFi.mode(WIFI_OFF);
  Serial.println("📴 Wi-Fi em modo Modem Sleep.");
}

void wifiWake() {
  Serial.print("📡 Ligando Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) { 
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("✅ Wi-Fi reconectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("❌ Falha ao reconectar o Wi-Fi.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=========================");
  Serial.println("   INICIANDO SENSOR    ");
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

  deviceId = getDeviceId();
  Serial.println("ID do dispositivo: " + deviceId);

  if (WiFi.status() != WL_CONNECTED) {
    conectaWiFi();
  }

  wifiSleep();
}

void loop() {
  float temperatura, umidade;

  if (!leSensor(temperatura, umidade)) {
    Serial.println("⚠️ Falha ao ler o sensor. Pulando esta leitura.");
    delay(INTERVALO_LEITURA);
    return;
  }

  temperatura = round(temperatura * 10) / 10.0;
  umidade = round(umidade * 10) / 10.0;

  bool alerta = (temperatura < TEMP_MIN || temperatura > TEMP_MAX || umidade < HUM_MIN || umidade > HUM_MAX);

  Serial.printf("🌡️ %.1f°C | 💧 %.1f%% | Alerta: %s\n", temperatura, umidade, alerta ? "SIM" : "NÃO");

  salvaDadoLocal(temperatura, umidade, alerta);

  leituras_count++;
  Serial.printf("📊 Leitura #%d\n", leituras_count);

  if (alerta || leituras_count >= CICLOS_ENVIO) {
    digitalWrite(LED_PIN, HIGH);
    wifiWake();

    if (WiFi.status() == WL_CONNECTED) {
      conectaMQTT();
      if (client.connected()) {
        enviaMQTT();
        leituras_count = 0;
      }
    } else {
      Serial.println("⚠️ Sem WiFi! Mantendo dados locais para próxima tentativa.");
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