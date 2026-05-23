#include <driver/i2s.h>

#define I2S_MIC_SCK    32
#define I2S_MIC_WS     33
#define I2S_MIC_SD     34

#define I2S_AMP_BCLK   15
#define I2S_AMP_LRC     2
#define I2S_AMP_DIN    19

#define SAMPLE_RATE    16000
#define SEGUNDOS        3
#define TOTAL_SAMPLES  (SAMPLE_RATE * SEGUNDOS)

int16_t grabacion[TOTAL_SAMPLES];

void setup() {
  Serial.begin(115200);
  Serial.println("=== Grabacion y reproduccion ===");
  Serial.println("Preparate para grabar en:");
  Serial.println("3...");
  delay(1000);
  Serial.println("2...");
  delay(1000);
  Serial.println("1...");
  delay(1000);

  i2s_config_t mic_cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 256,
    .use_apll             = false,
  };
  i2s_pin_config_t mic_pins = {
    .bck_io_num   = I2S_MIC_SCK,
    .ws_io_num    = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_MIC_SD,
  };
  i2s_driver_install(I2S_NUM_0, &mic_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &mic_pins);

  Serial.println("*** GRABANDO — habla ahora! ***");
  size_t totalLeido = 0;
  int segundoActual = 0;

  while (totalLeido < TOTAL_SAMPLES) {
    size_t bytesRead = 0;
    int16_t temp[256];
    i2s_read(I2S_NUM_0, temp, sizeof(temp), &bytesRead, portMAX_DELAY);
    int16_t muestras = bytesRead / 2;
    for (int i = 0; i < muestras && totalLeido < TOTAL_SAMPLES; i++) {
      int32_t amp = (int32_t)temp[i] * 2;
      if (amp > 32767)  amp = 32767;
      if (amp < -32768) amp = -32768;
      grabacion[totalLeido++] = (int16_t)amp;
    }
    int nuevoSegundo = totalLeido / SAMPLE_RATE;
    if (nuevoSegundo > segundoActual) {
      segundoActual = nuevoSegundo;
      int restantes = SEGUNDOS - segundoActual;
      if (restantes > 0) {
        Serial.print("Tiempo restante: ");
        Serial.print(restantes);
        Serial.println(" seg...");
      }
    }
  }

  Serial.println("Grabacion completa. Reproduciendo en 2 segundos...");
  i2s_driver_uninstall(I2S_NUM_0);
  delay(2000);

  i2s_config_t amp_cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 256,
    .use_apll             = false,
  };
  i2s_pin_config_t amp_pins = {
    .bck_io_num   = I2S_AMP_BCLK,
    .ws_io_num    = I2S_AMP_LRC,
    .data_out_num = I2S_AMP_DIN,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(I2S_NUM_1, &amp_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &amp_pins);

  Serial.println("*** REPRODUCIENDO ***");
  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_1, grabacion, TOTAL_SAMPLES * 2, &bytesWritten, portMAX_DELAY);
  i2s_zero_dma_buffer(I2S_NUM_1);
  Serial.println("Listo.");
}

void loop() {}