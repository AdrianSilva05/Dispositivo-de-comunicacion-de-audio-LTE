#include <driver/i2s.h>

//
// ===== MIC INMP441 =====
//
#define I2S_MIC_SCK   32
#define I2S_MIC_WS    33
#define I2S_MIC_SD    34

//
// ===== AMP MAX98357A =====
//
#define I2S_AMP_BCLK  26
#define I2S_AMP_LRC   25
#define I2S_AMP_DIN   19

//
// ===== AUDIO =====
//
#define SAMPLE_RATE      8000
#define BUFFER_SAMPLES    256

//
// ===== DELAY =====
//
#define DELAY_SECONDS      0.5f
#define DELAY_SAMPLES ((int)(SAMPLE_RATE * DELAY_SECONDS))

//
// Buffer circular
//
int16_t delayBuffer[DELAY_SAMPLES];

//
// Índices
//
volatile uint32_t writeIndex = 0;
volatile uint32_t readIndex  = 0;

//
// Buffer temporal DMA
//
int16_t audioChunk[BUFFER_SAMPLES];

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("================================");
  Serial.println(" Delay I2S 2 segundos");
  Serial.println("================================");

  //
  // =========================
  // MICRÓFONO
  // =========================
  //
  i2s_config_t mic_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t mic_pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  //
  // =========================
  // AMPLIFICADOR
  // =========================
  //
  i2s_config_t amp_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t amp_pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_AMP_BCLK,
    .ws_io_num = I2S_AMP_LRC,
    .data_out_num = I2S_AMP_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  //
  // Instalar I2S MIC
  //
  esp_err_t err;

  err = i2s_driver_install(I2S_NUM_0, &mic_cfg, 0, NULL);

  if (err != ESP_OK) {
    Serial.print("Error MIC install: ");
    Serial.println(err);
    while (1);
  }

  err = i2s_set_pin(I2S_NUM_0, &mic_pins);

  if (err != ESP_OK) {
    Serial.print("Error MIC pins: ");
    Serial.println(err);
    while (1);
  }

  //
  // Instalar I2S AMP
  //
  err = i2s_driver_install(I2S_NUM_1, &amp_cfg, 0, NULL);

  if (err != ESP_OK) {
    Serial.print("Error AMP install: ");
    Serial.println(err);
    while (1);
  }

  err = i2s_set_pin(I2S_NUM_1, &amp_pins);

  if (err != ESP_OK) {
    Serial.print("Error AMP pins: ");
    Serial.println(err);
    while (1);
  }

  //
  // Limpiar buffer delay
  //
  memset(delayBuffer, 0, sizeof(delayBuffer));

  //
  // Inicializar índices
  //
  writeIndex = 0;
  readIndex = 0;

  Serial.println("Sistema iniciado.");
  Serial.println("Habla al micrófono...");
}

void loop() {

  size_t bytesRead = 0;
  size_t bytesWritten = 0;

  //
  // LEER MICRÓFONO
  //
  esp_err_t result = i2s_read(
                       I2S_NUM_0,
                       audioChunk,
                       sizeof(audioChunk),
                       &bytesRead,
                       portMAX_DELAY
                     );

  if (result != ESP_OK) {
    Serial.println("Error leyendo mic");
    return;
  }

  //
  // Cantidad de muestras
  //
  int samples = bytesRead / sizeof(int16_t);

  //
  // Procesar muestras
  //
  for (int i = 0; i < samples; i++) {

    //
    // Guardar muestra original
    //
    int16_t sample = audioChunk[i];

    //
    // Reproducir muestra vieja
    //
    audioChunk[i] = delayBuffer[readIndex];

    //
    // Guardar muestra nueva
    //
    delayBuffer[writeIndex] = sample;

    //
    // Avanzar índices
    //
    writeIndex++;
    readIndex++;

    //
    // Reiniciar circularmente
    //
    if (writeIndex >= DELAY_SAMPLES)
      writeIndex = 0;

    if (readIndex >= DELAY_SAMPLES)
      readIndex = 0;
  }

  //
  // REPRODUCIR AUDIO ATRASADO
  //
  result = i2s_write(
             I2S_NUM_1,
             audioChunk,
             bytesRead,
             &bytesWritten,
             portMAX_DELAY
           );

  if (result != ESP_OK) {
    Serial.println("Error escribiendo audio");
  }
}
