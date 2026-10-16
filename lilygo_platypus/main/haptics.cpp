#include "haptics.h"
#include "HapticDriver_DRV2605.hpp"
#include "HapticDriver_DRV2605_Reg.hpp"
#include "PmicAXP2101.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "haptics";
static HapticDriver_DRV2605 drv;
static PmicAXP2101 pmic;
static TaskHandle_t task;
// Match PlatypusVibration's timeout, independent of signal strength.
static constexpr uint32_t VIBRATION_DURATION_MS = 5000;
// Signed open-loop RTP: 0 stops, 1..127 drives forward. Keep level 1 at 40
// (already similar to Android), but soften higher levels for cross-watch
// matching. This cap is an empirical tuning value, not full-scale drive.
static constexpr uint8_t MIN_AMPLITUDE = 40;
static constexpr uint8_t
MAX_AMPLITUDE = 60;
static constexpr uint8_t ERM_OPEN_LOOP = 0x20;
static constexpr uint8_t DATA_FORMAT_RTP = 0x08;

static void stop_motor(void) {
  drv.setRealtimeValue(0); // stop() alone does not stop RTP playback
  drv.setMode(HapticMode::STANDBY);
}

static void haptic_task(void *arg) {
  TickType_t wait = portMAX_DELAY;
  for (;;) {
    uint32_t strength;
    if (!xTaskNotifyWait(0, UINT32_MAX, &strength, wait) || strength == 0) {
      stop_motor();
      wait = portMAX_DELAY;
      continue;
    }
    uint8_t amplitude = MIN_AMPLITUDE +
                        (strength - 1) * (MAX_AMPLITUDE - MIN_AMPLITUDE) / 9;
    drv.setMode(HapticMode::REAL_TIME_PLAYBACK);
    drv.setRealtimeValue(amplitude);
    wait = pdMS_TO_TICKS(VIBRATION_DURATION_MS);
    if (wait == 0)
      wait = 1;
  }
}

void haptic_init(void) {
  i2c_master_bus_config_t config = {};
  config.i2c_port = I2C_NUM_0;
  config.sda_io_num = GPIO_NUM_10;
  config.scl_io_num = GPIO_NUM_11;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus;
  ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus));

  // T-Watch S3: enable the motor driver's power rail, like LilyGoLib does.
  if (!pmic.begin(bus, AXP2101_SLAVE_ADDRESS) ||
      !pmic.getChannel()->setVoltage(AXP2101Channel::CH_BLDO2, 3300) ||
      !pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, true)) {
    ESP_LOGE(TAG, "Haptic power unavailable");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(5));
  if (!drv.begin(bus, DRV2605_SLAVE_ADDRESS)) {
    ESP_LOGE(TAG, "DRV2605 not found (haptics disabled)");
    return;
  }
  // Use signed RTP in ERM open-loop mode: unsigned RTP is centered at 127,
  // so mapping 0..255 would reverse the motor below the midpoint, not scale
  // vibration strength. Clear DATA_FORMAT_RTP so zero really means stop.
  if (drv.updateBits(DRV2605Regs::DRV2605_REG_CONTROL3,
                     ERM_OPEN_LOOP | DATA_FORMAT_RTP, ERM_OPEN_LOOP) < 0) {
    ESP_LOGE(TAG, "DRV2605 configuration failed");
    stop_motor();
    return;
  }
  stop_motor();
  if (xTaskCreate(haptic_task, "haptics", 4096, NULL, 2, &task) != pdPASS)
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  ESP_LOGI(TAG, "SensorLib haptics ready");
}

void apply_haptic(int strength) {
  if (!task)
    return;
  if (strength < 0) strength = 0;
  if (strength > 10) strength = 10;
  xTaskNotify(task, strength, eSetValueWithOverwrite);
}
