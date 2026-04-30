#include <HardwareSerial.h>

// Pines correctos según pinout T-A7670G R2
#define MODEM_TX     26
#define MODEM_RX     27
#define MODEM_PWRKEY  4
#define MODEM_RST    12

HardwareSerial modemSerial(1);

void sendAT(const char* cmd, int waitMs = 2000) {
  Serial.print(">>> ");
  Serial.println(cmd);
  modemSerial.println(cmd);
  long start = millis();
  while (millis() - start < waitMs) {
    while (modemSerial.available()) {
      Serial.write(modemSerial.read());
    }
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Iniciando LilyGO T-A7670G R2 ===");

  // Encender módulo A7670G
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(2000);
  digitalWrite(MODEM_PWRKEY, LOW);

  modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(5000); // Esperar que el módem arranque completamente
  Serial.println("Enviando comandos AT...\n");

  sendAT("AT");           // Verificar comunicación
  sendAT("AT+CPIN?");    // Estado SIM — debe decir READY
  sendAT("AT+CSQ");      // Calidad de señal
  sendAT("AT+CREG?");    // Registro en red
  sendAT("AT+COPS?");    // Operador — debe mostrar WOM
}

void loop() {
  while (modemSerial.available()) {
    Serial.write(modemSerial.read());
  }
  while (Serial.available()) {
    modemSerial.write(Serial.read());
  }
}