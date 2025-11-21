# Sensores-MiteScan

Este repositório contém o firmware para o dispositivo sensor do projeto MiteScan, baseado em um ESP32 e no sensor de temperatura e umidade AHT10.

## ✨ Funcionalidades

- **Leitura de Sensores**: Coleta dados de temperatura e umidade do AHT10.
- **WiFi Manager**: Permite configurar as credenciais de Wi-Fi, nome da colmeia e conta do usuário através de um portal cativo, sem precisar alterar o código.
- **Comunicação MQTT**: Envia os dados coletados para um broker MQTT público (`broker.hivemq.com`) em formato JSON.
- **Modo de Economia de Energia (Deep Sleep)**: Utiliza o modo de sono profundo do ESP32 para reduzir drasticamente o consumo. O ciclo é: acordar, ler, conectar, enviar e dormir por 3 minutos.
- **Geração de Alertas**: Envia dados para um tópico MQTT específico (`colmeia/alerta`) se os valores de temperatura ou umidade estiverem fora dos limites pré-definidos.
- **Sinalização Visual (LED)**: Fornece feedback sobre o status da operação através do LED integrado.

---

## 🚀 Instalação

1.  **Hardware**: Conecte o sensor AHT10 ao ESP32:
    - `VIN` → `3.3V`
    - `GND` → `GND`
    - `SCL` → `GPIO 22`
    - `SDA` → `GPIO 21`

2.  **Software**: Abra o projeto com PlatformIO (VS Code). As bibliotecas (`Adafruit AHTX0`, `WiFiManager`, `PubSubClient`, `ArduinoJson`) serão instaladas automaticamente.

3.  **Gravação**: Conecte o ESP32 via USB e use a função `Upload` do PlatformIO. Monitore a saída serial em `115200` baud.

---

## 📶 Configuração Inicial (WiFiManager)

Na primeira vez que o dispositivo ligar, ele criará uma rede Wi-Fi para configuração:

1.  **Conecte-se** à rede Wi-Fi **"ColmeiaSetup"** com seu celular ou computador.
2.  **Acesse o portal** que abrirá automaticamente (ou navegue para `192.168.4.1`).
3.  **Configure** sua rede Wi-Fi, o **Nome da Colmeia** e a **Conta do Usuário**.
4.  **Salve**. O dispositivo irá reiniciar e se conectar automaticamente a partir de agora.

---

## 📊 Formato dos Dados Enviados (Payload MQTT)

O dispositivo envia os dados no seguinte formato JSON para os tópicos `colmeia/dados` ou `colmeia/alerta` (em caso de valores fora do padrão):

```json
{
  "h": 65.5,                  // Umidade relativa (%)
  "t": 32.1,                  // Temperatura (°C)
  "id": "A1:B2:C3:D4:E5:F6",  // ID único do dispositivo (MAC Address)
  "nome_colmeia": "Colmeia da Figueira",
  "conta_usuario": "usuario@email.com"
}
```