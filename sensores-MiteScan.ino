#include <dht11.h>
#define PINODHT11 2

dht11 DHT11;

void setup() {
  Serial.begin(9600)

}

void loop() {
  
  DHT11.read(PINODHT11);

  Serial.print("Umidade: ")
  Serial.print(DHT11.humidity)
  Serial.print("%")

  Serial.print("Temperatura: ")
  Serial.print(DHT11.temperature)
  Serial.print("C")
  Serial.print("______________________________")

  delay(2000)

}
