/*
 * ============================================================
 *  TRANSMISOR AUDIO — TTGO T-A7670G R2 + CLARO  (Módulo A)
 *  Micrófono INMP441 → I2S → paquetes UDP → servidor relay
 *  Sample rate: 8000 Hz, 16-bit mono
 *  Paquete: 320 bytes (160 muestras = 20ms de audio)
 * ============================================================
 */

#include <HardwareSerial.h>
#include <driver/i2s.h>

// ── RED ─────────────────────────────────────────────────────
#define SERVER_IP      "52.54.41.85"
#define PORT_AUDIO     5004
#define APN            "internet.claro.com.co"
#define LOCAL_PORT     6011

// ── MODEM ───────────────────────────────────────────────────
#define MODEM_TX       26
#define MODEM_RX       27
#define MODEM_PWRKEY    4
#define MODEM_DTR      25
#define MODEM_POWER_EN 12
#define LED_PIN         2
#define UDP_SOCKET_ID   0

// ── I2S MICRÓFONO INMP441 ───────────────────────────────────
#define I2S_MIC_PORT    I2S_NUM_0
#define I2S_MIC_SCK     32
#define I2S_MIC_WS      33
#define I2S_MIC_SD      34
#define SAMPLE_RATE     8000
#define SAMPLES_PER_PKT 160    // 160 muestras × 2 bytes = 320 bytes = 20ms
#define BYTES_PER_PKT   (SAMPLES_PER_PKT * 2)  // 320 bytes

// ── BUFFERS ─────────────────────────────────────────────────
int32_t  i2s_raw[SAMPLES_PER_PKT];        // buffer I2S crudo (32-bit)
int16_t  audio_pkt[SAMPLES_PER_PKT];      // buffer audio 16-bit
uint8_t  send_buf[BYTES_PER_PKT];         // buffer para CIPSEND

HardwareSerial modemSerial(1);
unsigned long pktCount = 0;

// ============================================================
//  LED
// ============================================================
void ledBlink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(onMs);
    digitalWrite(LED_PIN, LOW);  delay(offMs);
  }
}

// ============================================================
//  AT helpers
// ============================================================
bool waitFor(const char* token, int timeout = 15000) {
  String buf = "";
  long t = millis();
  while (millis() - t < timeout) {
    while (modemSerial.available()) {
      char c = (char)modemSerial.read();
      Serial.write(c);
      buf += c;
      if (buf.length() > 400) buf = buf.substring(200);
      if (buf.endsWith(token)) return true;
    }
  }
  return false;
}

String sendATwait(const char* cmd, int timeout = 8000) {
  while (modemSerial.available()) modemSerial.read();
  modemSerial.println(cmd);
  Serial.print(">>> "); Serial.println(cmd);
  String resp = "";
  long start = millis();
  while (millis() - start < timeout) {
    while (modemSerial.available()) {
      char c = (char)modemSerial.read();
      resp += c;
      Serial.write(c);
    }
    if (resp.indexOf("\nOK")       >= 0) return resp;
    if (resp.indexOf("\nERROR")    >= 0) return resp;
    if (resp.indexOf("+CME ERROR") >= 0) return resp;
  }
  Serial.println("[AT] Timeout");
  return resp;
}

bool sendAT(const char* cmd, int timeout = 8000) {
  return sendATwait(cmd, timeout).indexOf("OK") >= 0;
}

// ============================================================
//  BOOT MODEM
// ============================================================
void modemPowerOn() {
  Serial.println("\n[MODEM] Encendiendo...");
  pinMode(MODEM_POWER_EN, OUTPUT);
  pinMode(MODEM_DTR,      OUTPUT);
  pinMode(MODEM_PWRKEY,   OUTPUT);
  pinMode(LED_PIN,        OUTPUT);

  digitalWrite(MODEM_POWER_EN, HIGH);
  digitalWrite(MODEM_DTR,      LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);

  Serial.println("[MODEM] Esperando arranque (5s)...");
  delay(5000);

  Serial.println("[MODEM] Esperando PB DONE + SMS DONE (max 40s)...");
  String bootLog = "";
  long start = millis();
  bool pbDone = false, smsDone = false;
  while (millis() - start < 40000) {
    while (modemSerial.available()) {
      char c = (char)modemSerial.read();
      bootLog += c; Serial.write(c);
      if (bootLog.length() > 600) bootLog = bootLog.substring(300);
    }
    if (bootLog.indexOf("PB DONE")  >= 0) pbDone  = true;
    if (bootLog.indexOf("SMS DONE") >= 0) smsDone = true;
    if (pbDone && smsDone) {
      Serial.println("\n[MODEM] ✓ Boot completo");
      ledBlink(5, 100, 100);
      break;
    }
  }

  Serial.println("[MODEM] Estabilizando (8s)...");
  delay(8000);
  sendAT("ATE0", 2000);
  for (int i = 0; i < 10; i++) {
    if (sendAT("AT", 3000)) { Serial.println("[MODEM] ✓ AT OK"); return; }
    delay(2000);
  }
}

// ============================================================
//  LTE
// ============================================================
bool connectLTE() {
  sendAT("AT+CMEE=2");
  sendAT("AT+CNMP=38");

  Serial.println("[LTE] Verificando SIM...");
  for (int i = 0; i < 20; i++) {
    if (sendATwait("AT+CPIN?", 4000).indexOf("READY") >= 0) {
      Serial.println("[LTE] ✓ SIM OK"); break;
    }
    delay(1200);
  }

  sendATwait("AT+CSQ", 2000);
  char cmd[80];
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
  sendAT(cmd);
  sendAT("AT+CGATT=1", 4000);
  delay(1500);

  String cgact = sendATwait("AT+CGACT=1,1", 4000);
  if (cgact.indexOf("ERROR") >= 0)
    Serial.println("[LTE] CGACT: PDP ya activo — OK");
  delay(1000);

  String netState = sendATwait("AT+NETOPEN?", 5000);
  if (netState.indexOf("+NETOPEN: 1") >= 0) {
    Serial.println("[LTE] ✓ Red ya abierta");
  } else {
    String r = sendATwait("AT+NETOPEN", 10000);
    if (r.indexOf("already opened") < 0)
      waitFor("+NETOPEN: 0", 15000);
  }
  delay(5000);

  sendAT("AT+CIPRXGET=0");
  delay(500);

  String ipResp = sendATwait("AT+CGPADDR=1", 5000);
  if (ipResp.indexOf("ERROR") >= 0 || ipResp.indexOf("0.0.0.0") >= 0) {
    Serial.println("[LTE] ERROR: Sin IP"); return false;
  }
  Serial.println("[LTE] ✓ LTE OK");
  ledBlink(5, 100, 100);
  return true;
}

// ============================================================
//  ABRIR SOCKET UDP
// ============================================================
bool openUDP() {
  sendATwait("AT+CIPCLOSE=0", 5000);
  delay(2000);

  for (int intento = 0; intento < 3; intento++) {
    int puerto = LOCAL_PORT + (intento * 10);
    char cmd[50];
    snprintf(cmd, sizeof(cmd),
             "AT+CIPOPEN=%d,\"UDP\",,,%d", UDP_SOCKET_ID, puerto);

    while (modemSerial.available()) modemSerial.read();
    modemSerial.println(cmd);
    Serial.print(">>> "); Serial.println(cmd);

    String r = "";
    long start = millis();
    while (millis() - start < 20000) {
      while (modemSerial.available()) {
        char c = (char)modemSerial.read(); r += c; Serial.write(c);
      }
      if (r.indexOf("+CIPOPEN: 0,0") >= 0) {
        Serial.println("[UDP] ✓ Socket abierto");
        ledBlink(5, 100, 100);
        return true;
      }
      if (r.indexOf("ERROR") >= 0) break;
    }

    if (r.indexOf("+CIPOPEN: 0,2") >= 0) {
      sendATwait("AT+CIPCLOSE=0", 3000); delay(500);
      sendATwait("AT+NETCLOSE",   8000);
      waitFor("+NETCLOSE: 0", 5000); delay(3000);
      sendATwait("AT+NETOPEN",   10000);
      waitFor("+NETOPEN: 0", 15000); delay(5000);
    } else {
      sendATwait("AT+CIPCLOSE=0", 3000); delay(2000);
    }
  }
  return false;
}

// ============================================================
//  I2S MICRÓFONO
// ============================================================
void setupMic() {
  i2s_config_t i2s_cfg = {};
  i2s_cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_cfg.sample_rate          = SAMPLE_RATE;
  i2s_cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;  // INMP441 entrega 32-bit
  i2s_cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  i2s_cfg.dma_buf_count        = 4;
  i2s_cfg.dma_buf_len          = SAMPLES_PER_PKT;
  i2s_cfg.use_apll             = false;
  i2s_cfg.tx_desc_auto_clear   = false;
  i2s_cfg.fixed_mclk           = 0;

  i2s_pin_config_t pin_cfg = {};
  pin_cfg.bck_io_num           = I2S_MIC_SCK;
  pin_cfg.ws_io_num            = I2S_MIC_WS;
  pin_cfg.data_in_num          = I2S_MIC_SD;
  pin_cfg.data_out_num         = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_MIC_PORT, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &pin_cfg);
  i2s_zero_dma_buffer(I2S_MIC_PORT);

  Serial.println("[MIC] ✓ INMP441 inicializado (8kHz, 16-bit mono)");
}

// ============================================================
//  ENVIAR PAQUETE AUDIO POR UDP (binario)
// ============================================================
bool sendAudioPacket(const uint8_t* data, int len) {
  char cmd[80];
  snprintf(cmd, sizeof(cmd),
           "AT+CIPSEND=%d,%d,\"%s\",%d",
           UDP_SOCKET_ID, len, SERVER_IP, PORT_AUDIO);

  while (modemSerial.available()) modemSerial.read();
  modemSerial.println(cmd);

  // Esperar prompt >
  long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 2000) {
    while (modemSerial.available()) {
      char c = (char)modemSerial.read();
      if (c == '>') { gotPrompt = true; break; }
    }
    if (gotPrompt) break;
  }

  if (!gotPrompt) {
    Serial.println("[TX] Sin prompt >");
    return false;
  }

  // Enviar datos binarios crudos
  modemSerial.write(data, len);

  // Esperar confirmación
  String resp = "";
  start = millis();
  while (millis() - start < 2000) {
    while (modemSerial.available()) {
      char c = (char)modemSerial.read();
      resp += c;
    }
    if (resp.indexOf("+CIPSEND:") >= 0 || resp.indexOf("ERROR") >= 0) break;
  }

  return resp.indexOf("+CIPSEND:") >= 0;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   TRANSMISOR AUDIO — T-A7670G + CLARO  ║");
  Serial.println("║   Módulo A — INMP441 → LTE → servidor  ║");
  Serial.println("║   8kHz 16-bit mono, 320 bytes/pkt      ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  Serial.println("[BOOT] Estabilizando alimentación (5s)...");
  delay(5000);

  modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  modemPowerOn();

  if (!connectLTE()) {
    Serial.println("[FATAL] Sin LTE");
    ledBlink(3, 500, 500); delay(10000); ESP.restart();
  }
  if (!openUDP()) {
    Serial.println("[FATAL] Sin socket");
    ledBlink(3, 500, 500); delay(10000); ESP.restart();
  }

  setupMic();

  Serial.println("\n[OK] ✓ Transmitiendo audio\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Leer 160 muestras del micrófono (32-bit cada una)
  size_t bytesRead = 0;
  i2s_read(I2S_MIC_PORT, i2s_raw, sizeof(i2s_raw), &bytesRead, portMAX_DELAY);

  int samplesRead = bytesRead / 4;  // 4 bytes por muestra de 32-bit

  // Convertir 32-bit → 16-bit (tomar los 16 bits altos)
  for (int i = 0; i < samplesRead; i++) {
    audio_pkt[i] = (int16_t)(i2s_raw[i] >> 15);
  }

  // Copiar a buffer de envío
  int sendLen = samplesRead * 2;
  memcpy(send_buf, audio_pkt, sendLen);

  // Enviar por UDP
  if (sendAudioPacket(send_buf, sendLen)) {
    pktCount++;
    if (pktCount % 50 == 0) {
      Serial.print("[TX] Paquetes: "); Serial.println(pktCount);
    }
  }
}
