#include <Arduino.h>
#include <driver/i2s.h>
#include <HardwareSerial.h>

// ─── Pines A7670G ───────────────────────────────────────────
#define MODEM_TX       26
#define MODEM_RX       27
#define MODEM_PWRKEY    4
#define MODEM_RST      12

// ─── Pines INMP441 (micrófono) ──────────────────────────────
#define I2S_MIC_SCK    32
#define I2S_MIC_WS     33
#define I2S_MIC_SD     34

// ─── Pines MAX98357A (amplificador) ─────────────────────────
#define I2S_AMP_BCLK   25
#define I2S_AMP_LRC    22
#define I2S_AMP_DIN    21

// ─── DIP Switch ─────────────────────────────────────────────
#define SW1_ID         13
#define SW2_PTT        14

// ─── Servidor AWS ───────────────────────────────────────────
#define SERVER_IP      "52.90.229.246"
#define SERVER_PORT    5555

// ─── Audio ──────────────────────────────────────────────────
#define SAMPLE_RATE    16000
#define BUFFER_SIZE    512

HardwareSerial modemSerial(1);
bool deviceIsA = false;
bool connected = false;

// ────────────────────────────────────────────────────────────
void setupMic() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = BUFFER_SIZE,
    .use_apll             = false,
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_MIC_SCK,
    .ws_io_num    = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_MIC_SD,
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

void setupAmp() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = BUFFER_SIZE,
    .use_apll             = false,
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_AMP_BCLK,
    .ws_io_num    = I2S_AMP_LRC,
    .data_out_num = I2S_AMP_DIN,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
}

// ────────────────────────────────────────────────────────────
bool waitFor(const char* token, int timeout = 15000) {
  String buf = "";
  long t = millis();
  while (millis() - t < timeout) {
    while (modemSerial.available()) {
      char c = modemSerial.read();
      Serial.write(c);
      buf += c;
      if (buf.length() > 200) buf = buf.substring(100);
      if (buf.endsWith(token)) return true;
    }
  }
  return false;
}

void sendAT(const char* cmd, int wait = 3000) {
  modemSerial.println(cmd);
  long t = millis();
  while (millis() - t < wait) {
    while (modemSerial.available())
      Serial.write(modemSerial.read());
  }
}

void modemPowerOn() {
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(2000);
  digitalWrite(MODEM_PWRKEY, LOW);
}

bool connectTCP() {
  Serial.println("Conectando al servidor TCP...");
  modemSerial.println("AT+CIPOPEN=0,\"TCP\",\"52.90.229.246\",5555");
  return waitFor("+CIPOPEN: 0,0", 20000);
}

void sendTCP(uint8_t* data, size_t len) {
  modemSerial.println("AT+CIPSEND=0," + String(len));
  long t = millis();
  while (millis() - t < 2000) {
    if (modemSerial.find(">")) break;
  }
  modemSerial.write(data, len);
  delay(20);
}

void receiveTCP() {
  modemSerial.println("AT+CIPRXGET=2,0,512");
  delay(50);

  uint8_t buf[BUFFER_SIZE];
  size_t len = 0;
  long t = millis();

  while (millis() - t < 500) {
    if (modemSerial.find("\n")) break;
  }

  while (modemSerial.available() && len < BUFFER_SIZE) {
    buf[len++] = modemSerial.read();
  }

  if (len > 0) {
    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_1, buf, len, &bytesWritten, portMAX_DELAY);
  }
}

// ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Walkie-Talkie 4G LTE ===");

  pinMode(SW1_ID,  INPUT_PULLUP);
  pinMode(SW2_PTT, INPUT_PULLUP);

  deviceIsA = (digitalRead(SW1_ID) == LOW);
  Serial.print("Dispositivo: ");
  Serial.println(deviceIsA ? "A (Claro)" : "B (WOM)");

  setupMic();
  setupAmp();

  modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  modemPowerOn();

  Serial.println("Esperando módem...");
  waitFor("PB DONE", 30000);
  delay(1000);

  sendAT("AT");
  sendAT("AT+CPIN?");

  if (deviceIsA) {
    Serial.println("APN: Claro");
    sendAT("AT+CGDCONT=1,\"IP\",\"internet.claro.com.co\"", 3000);
    sendAT("AT+CGDCONT=2,\"IP\",\"internet.claro.com.co\"", 3000);
  } else {
    Serial.println("APN: WOM");
    sendAT("AT+CGDCONT=1,\"IP\",\"internet.wom.co\"", 3000);
  }

  Serial.println("Abriendo red...");
  sendAT("AT+NETOPEN", 8000);
  waitFor("+NETOPEN: 0", 10000);
  sendAT("AT+CIPRXGET=1", 2000);
  sendAT("AT+IPADDR", 3000);

  if (connectTCP()) {
    connected = true;
    Serial.println("¡Conectado al servidor!");
    Serial.println("SW2 ON = habla | SW2 OFF = escucha");
  } else {
    Serial.println("Error conectando al servidor. Reinicia.");
  }
}

// ────────────────────────────────────────────────────────────
void loop() {
  if (!connected) return;

  bool ptt = (digitalRead(SW2_PTT) == LOW);

  if (ptt) {
    // ── Habla: captura y envía ──
    uint8_t buf[BUFFER_SIZE];
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, buf, BUFFER_SIZE, &bytesRead, portMAX_DELAY);
    if (bytesRead > 0) sendTCP(buf, bytesRead);

  } else {
    // ── Escucha: recibe y reproduce ──
    receiveTCP();
  }
}