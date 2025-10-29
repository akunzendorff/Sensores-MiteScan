# Sensores-MiteScan

Este repositório contém o firmware para o dispositivo sensor do projeto MiteScan, baseado em um ESP32 e no sensor de temperatura e umidade AHT10.

## ✨ Funcionalidades

- **Leitura de Sensores**: Coleta dados de temperatura e umidade do AHT10.
- **WiFi Manager**: Permite configurar as credenciais de Wi-Fi e do servidor MQTT através de um portal cativo, sem precisar alterar o código.
- **Comunicação MQTT**: Envia os dados coletados para um broker MQTT em formato JSON.
- **Armazenamento Local**: Salva as leituras no sistema de arquivos LittleFS e as envia em lote, garantindo que nenhum dado seja perdido em caso de falha de conexão.
- **Modo de Economia de Energia**: Utiliza o Modem Sleep do Wi-Fi para reduzir o consumo de bateria entre os envios.
- **Geração de Alertas**: Envia dados para um tópico MQTT específico (`colmeia/alerta`) se os valores de temperatura ou umidade estiverem fora dos limites pré-definidos.

---

## 🚀 Como Configurar e Gravar

Siga os passos abaixo para compilar e gravar o firmware no seu ESP32.

### 📋 Pré-requisitos

1.  **Hardware**:
    - Uma placa de desenvolvimento ESP32.
    - Um sensor de temperatura e umidade AHT10.
    - Jumpers para a conexão.

2.  **Software**:
    - **Arduino IDE** ou **PlatformIO** (recomendado, via VS Code).
    - **Drivers da placa ESP32**: Certifique-se de que sua placa é reconhecida pelo computador.

### 🔌 1. Conexão do Hardware (Wiring)

Conecte o sensor AHT10 ao ESP32 usando os pinos I2C definidos no código:

- **AHT10 VIN** → **ESP32 3.3V**
- **AHT10 GND** → **ESP32 GND**
- **AHT10 SCL** → **ESP32 GPIO 22** (pino SCL)
- **AHT10 SDA** → **ESP32 GPIO 21** (pino SDA)

> **Atenção:** Verifique o datasheet da sua placa ESP32 para confirmar os pinos de alimentação e I2C.

### ⚙️ 2. Configuração do Ambiente (PlatformIO)

Se estiver usando PlatformIO (recomendado), o arquivo `platformio.ini` já cuidará da instalação das bibliotecas necessárias.

1.  Abra o projeto na pasta `Sensores-MiteScan` com o VS Code e a extensão PlatformIO.
2.  O PlatformIO irá detectar o `platformio.ini` e perguntará se você deseja instalar as dependências. Confirme.

As bibliotecas necessárias são:
- `adafruit/Adafruit AHTX0`
- `tzapu/WiFiManager`
- `knolleary/PubSubClient`
- `bblanchon/ArduinoJson`
- `lorol/LittleFS_esp32`

### 💾 3. Compilação e Gravação

1.  Conecte o ESP32 ao seu computador via USB.
2.  No PlatformIO, clique no ícone de seta (→) na barra de status inferior para compilar e gravar o código (`Upload`).
3.  Abra o **Serial Monitor** (ícone de tomada na barra de status) com a velocidade (`baud rate`) de **115200** para acompanhar os logs.

---

## 📶 Como Usar o WiFiManager (Primeira Execução)

Na primeira vez que o dispositivo ligar (ou se ele não conseguir se conectar a uma rede Wi-Fi conhecida), ele criará um Ponto de Acesso (Access Point).

1.  **Conecte-se à rede Wi-Fi**: No seu celular ou computador, procure por uma rede Wi-Fi com o nome **"ColmeiaSetup"** e conecte-se a ela.

2.  **Acesse o Portal Cativo**: Um portal de configuração deve abrir automaticamente. Se não abrir, acesse o endereço `192.168.4.1` no seu navegador.

3.  **Configure os Dados**:
    - Clique em **"Configure WiFi"**.
    - Selecione a sua rede Wi-Fi local e digite a senha.
    - Preencha os campos do servidor MQTT:
        - **MQTT Server**: O endereço IP do computador onde o broker MQTT está rodando (ex: `192.168.3.119`).
        - **MQTT Port**: A porta do broker (geralmente `1883`).
        - **MQTT User / Pass**: Usuário e senha do broker, se houver.

4.  **Salve as Configurações**: Clique em **"Save"**. O ESP32 irá salvar as informações, se conectar à sua rede Wi-Fi e reiniciar.

A partir de agora, o dispositivo se conectará automaticamente à rede configurada sempre que for ligado. Para reconfigurar, basta apagar as credenciais salvas ou garantir que ele não consiga se conectar à rede anterior.

## 📊 Formato dos Dados Enviados (Payload MQTT)

O dispositivo envia os dados no seguinte formato JSON para os tópicos `colmeia/dados` ou `colmeia/alerta`:

```json
{
  "h": 65.5,       // Umidade relativa (%)
  "t": 32.1,       // Temperatura (°C)
  "ts": 12345678,  // Timestamp (millis() desde o boot)
  "id": "A1B2C3D4E5F6" // ID único do dispositivo (baseado no MAC Address)
}
```