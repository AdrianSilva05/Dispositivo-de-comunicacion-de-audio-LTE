/*
 * ============================================================
 *  RECEPTOR AUDIO — TTGO T-A7670G R2 + CLARO  (Módulo B)
 *  Jitter buffer de 1 segundo con dos tareas FreeRTOS:
 *  - Tarea FETCH (Core 1): llena el buffer sin parar
 *  - Tarea I2S  (Core 0): reproduce el buffer sin parar
 *  8kHz 16-bit mono, 4×320 bytes por FETCH = 80ms por viaje
 * ============================================================
 */

#include <HardwareSerial.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── RED ─────────────────────────────────────────────────────
#define SERVER_IP      "52.54.41.85"
#define PORT_FETCH     5006
#define APN            "internet.claro.com.co"
#define LOCAL_PORT     6002

// ── MODEM ───────────────────────────────────────────────────
#define MODEM_TX       26
#define MODEM_RX       27
#define MODEM_PWRKEY    4
#define MODEM_DTR      25
#define MODEM_POWER_EN 12
#define LED_PIN         2
#define UDP_SOCKET_ID   0

// ── I2S PARLANTE MAX98357A ──────────────────────────────────
#define I2S_SPK_PORT    I2S_NUM_1
#define I2S_SPK_BCLK    15
#define I2S_SPK_LRC      2
#define I2S_SPK_DIN     19
#define SAMPLE_RATE     8000
#define SAMPLES_PER_PKT 160
#define BYTES_PER_PKT   320

// ── JITTER BUFFER ───────────────────────────────────────────
// 1 segundo de audio = 1000ms / 20ms por paquete = 50 paquetes
// Usamos 64 para tener margen (potencia de 2 para el índice circular)
#define BUF_SLOTS       128  // era 64
#define BUF_MASK        (BUF_SLOTS - 1)
#define MIN_PKTS_START  18   // era 12 — ahora ~360ms antes de arrancar
#define MIN_PKTS_RESUME 12   // era 8  — ahora ~240ms para reanudar

// Buffer circular de paquetes
typedef struct {
  uint8_t data[BYTES_PER_PKT];
  int     len;
} Slot;

static Slot     ring[BUF_SLOTS];
static volatile int  head = 0;   // próximo slot a escribir (FETCH)
static volatile int  tail = 0;   // próximo slot a leer    (I2S)
static volatile int  count = 0;  // paquetes disponibles
static SemaphoreHandle_t ringMutex;

// ── UART MODEM ───────────────────────────────────────────────
HardwareSerial modemSerial(1);

// Silencio para cuando el buffer se vacía
static uint8_t silence[BYTES_PER_PKT] = {0};

// ============================================================
//  Funciones del buffer circular
// ============================================================
bool bufPush(const uint8_t* data, int len) {
  bool ok = false;
  if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (count < BUF_SLOTS) {
      memcpy(ring[head].data, data, len);
      ring[head].len = len;
      head = (head + 1) & BUF_MASK;
      count++;
      ok = true;
    }
    xSemaphoreGive(ringMutex);
  }
  return ok;
}

bool bufPop(Slot* out) {
  bool ok = false;
  if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (count > 0) {
      memcpy(out->data, ring[tail].data, ring[tail].len);
      out->len = ring[tail].len;
      tail = (tail + 1) & BUF_MASK;
      count--;
      ok = true;
    }
    xSemaphoreGive(ringMutex);
  }
  return ok;
}

int bufCount() {
  int c = 0;
  if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
    c = count;
    xSemaphoreGive(ringMutex);
  }
  return c;
}

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
//  I2S PARLANTE MAX98357A
// ============================================================
void setupSpeaker() {
  i2s_config_t i2s_cfg = {};
  i2s_cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_cfg.sample_rate          = SAMPLE_RATE;
  i2s_cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  i2s_cfg.dma_buf_count        = 8;
  i2s_cfg.dma_buf_len          = SAMPLES_PER_PKT;
  i2s_cfg.use_apll             = false;
  i2s_cfg.tx_desc_auto_clear   = true;
  i2s_cfg.fixed_mclk           = 0;

  i2s_pin_config_t pin_cfg = {};
  pin_cfg.bck_io_num           = I2S_SPK_BCLK;
  pin_cfg.ws_io_num            = I2S_SPK_LRC;
  pin_cfg.data_out_num         = I2S_SPK_DIN;
  pin_cfg.data_in_num          = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_SPK_PORT, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &pin_cfg);
  i2s_zero_dma_buffer(I2S_SPK_PORT);

  Serial.println("[SPK] ✓ MAX98357A inicializado (8kHz, 16-bit mono)");
}

// ============================================================
//  TAREA I2S — Core 0
//  Reproduce continuamente desde el buffer circular
//  Espera MIN_PKTS_START antes de arrancar
//  Si se vacía espera MIN_PKTS_RESUME antes de reanudar
// ============================================================
void taskI2S(void* param) {
  Slot slot;
  bool buffering = true;
  size_t written = 0;

  Serial.println("[I2S] Tarea iniciada — esperando buffer...");

  while (true) {
    if (buffering) {
      int threshold = buffering ? MIN_PKTS_START : MIN_PKTS_RESUME;
      if (bufCount() >= threshold) {
        buffering = false;
        Serial.printf("[I2S] Buffer listo (%d pkts) — reproduciendo\n", bufCount());
      } else {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
    }

    if (bufPop(&slot)) {
      i2s_write(I2S_SPK_PORT, slot.data, slot.len, &written, portMAX_DELAY);
      // Delay para igualar tasa de consumo con tasa de producción
      // FETCH tarda 160ms y trae 4 paquetes → 40ms por paquete
      // I2S reproduce 1 paquete de 20ms → necesita esperar 20ms más
      vTaskDelay(pdMS_TO_TICKS(60));  // era 20, prueba con 60
    } else {
      i2s_write(I2S_SPK_PORT, silence, BYTES_PER_PKT, &written, portMAX_DELAY);
      buffering = true;
      Serial.printf("[I2S] Buffer vacío — reacumulando\n");
    }
  }
}

// ============================================================
//  TAREA FETCH — Core 1 (loop principal)
//  Hace FETCHes sin parar y mete paquetes al buffer circular
// ============================================================
void doFetch() {
  static uint8_t fetch_buf[1280];

  // Limpiar buffer UART
  while (modemSerial.available()) modemSerial.read();

  // Enviar FETCH
  char cmd[80];
  snprintf(cmd, sizeof(cmd),
           "AT+CIPSEND=0,5,\"%s\",%d", SERVER_IP, PORT_FETCH);
  modemSerial.println(cmd);

  // Esperar >
  long t = millis();
  bool got = false;
  while (millis() - t < 3000) {
    while (modemSerial.available()) {
      if ((char)modemSerial.read() == '>') { got = true; break; }
    }
    if (got) break;
  }
  if (!got) return;

  modemSerial.print("FETCH");

  // Leer header hasta +IPD<len>\n
  String header = "";
  int payloadLen = 0;
  bool foundIPD = false;
  t = millis();

  while (millis() - t < 1500 && !foundIPD) {
    while (modemSerial.available()) {
      char c = (char)modemSerial.read();
      header += c;
      int idx = header.indexOf("+IPD");
      if (idx >= 0) {
        int nl = header.indexOf('\n', idx);
        if (nl >= 0) {
          String lenStr = header.substring(idx + 4, nl);
          lenStr.trim();
          payloadLen = lenStr.toInt();
          foundIPD = true;
          break;
        }
      }
    }
  }

  if (!foundIPD || payloadLen <= 0) return;

  // EMPTY
  if (payloadLen == 5) {
    int d = 0; t = millis();
    while (d < 5 && millis() - t < 200) {
      if (modemSerial.available()) { modemSerial.read(); d++; }
    }
    return;
  }

  // Leer datos binarios
  int pos = 0;
  t = millis();
  while (pos < payloadLen && pos < 1280 && millis() - t < 800) {
    if (modemSerial.available()) {
      fetch_buf[pos++] = (uint8_t)modemSerial.read();
    }
  }

  if (pos <= 0) return;

  // Dividir en paquetes de BYTES_PER_PKT y meter al buffer circular
  int offset = 0;
  while (offset + BYTES_PER_PKT <= pos) {
    bufPush(fetch_buf + offset, BYTES_PER_PKT);
    offset += BYTES_PER_PKT;
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   RECEPTOR AUDIO — T-A7670G + CLARO    ║");
  Serial.println("║   Jitter buffer 1s — 8kHz 16-bit mono  ║");
  Serial.println("╚════════════════════════════════════════╝\n");

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

  setupSpeaker();

  // Crear mutex del buffer circular
  ringMutex = xSemaphoreCreateMutex();

  // Lanzar tarea I2S en Core 0, prioridad alta
  xTaskCreatePinnedToCore(
    taskI2S,
    "taskI2S",
    4096,
    NULL,
    5,
    NULL,
    0
  );

  Serial.println("\n[OK] ✓ Listo — llenando buffer antes de reproducir\n");
}

// ============================================================
//  LOOP — Core 1: hace FETCHes sin parar
// ============================================================
void loop() {
  doFetch();
  // Sin delay — el FETCH ya tarda ~160ms de por sí
  // Loguear estado del buffer cada ~5 segundos
  static unsigned long lastLog = 0;
  static unsigned long fetchCount = 0;
  fetchCount++;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[FETCH] viajes=%lu  cola=%d\n", fetchCount, bufCount());
  }
}
