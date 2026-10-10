#include "cJSON.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#define I2C_SDA_PIN 10
#define I2C_SCL_PIN 11
#define I2C_TIMEOUT_MS 100
#define I2C_SCL_SPEED_HZ 400000
#define I2C_GLITCH_IGNORE_CYCLES 7

#define DRV2605_ADDR 0x5A
#define REG_MODE 0x01
#define REG_RTP_INPUT 0x02
#define REG_CONTROL3 0x1D
#define DRV_MODE_STANDBY (1U << 6) // MODE register: STANDBY bit
#define DRV_MODE_RESET (1U << 7)   // MODE register: DEV_RESET bit
#define DRV_MODE_RTP 0x05          // MODE[2:0]: real-time playback
#define DRV_CONTROL3_ERM_OPEN_LOOP (1U << 5)
#define DRV_CONTROL3_RTP_UNSIGNED                                              \
  (1U << 3) // DATA_FORMAT_RTP: 0 = signed, 1 = unsigned
#define CONTROL3_VALUE (DRV_CONTROL3_ERM_OPEN_LOOP | DRV_CONTROL3_RTP_UNSIGNED)
#define DRV_RESET_DELAY_MS 5
#define DRV_READ_FALLBACK UINT8_MAX // returned when no device is available

#define HAPTIC_STRENGTH_OFF 0
#define HAPTIC_STRENGTH_MIN 1
#define HAPTIC_STRENGTH_MAX 10
#define HAPTIC_MIN_AMP 40        // amplitude for strength 1
#define HAPTIC_MAX_AMP UINT8_MAX // full-scale unsigned RTP input
#define HAPTIC_PULSE_US 200000 // maximum pulse duration: 200 ms at strength 10
#define HAPTIC_MIN_PULSE_US 1000 // minimum pulse duration: 1 ms

#define BL_PIN 45        // T-Watch S3 display backlight
#define BL_MIN_DUTY 30   // dimmest point of the pulse (0..1023)
#define BL_MAX_DUTY 1023 // brightest point of the pulse
#define BL_STEP 16
#define BL_STEP_MS 20 // full dim-to-bright sweep is about 1.2 s
#define BL_PWM_FREQ_HZ 5000
#define BL_TASK_STACK_BYTES 2048
#define BL_TASK_PRIORITY 1

#define MQTT_PAYLOAD_BUFFER_SIZE 64 // includes the terminating NUL
#define MQTT_SUBSCRIBE_QOS 0        // at-most-once delivery

static const char *TAG = "mqtt_sub";

static i2c_master_dev_handle_t drv = NULL;
static esp_timer_handle_t stop_timer = NULL;
static esp_mqtt_client_handle_t client;
static bool mqtt_started = false;

/* ---------------- Backlight pulse ---------------- */

static void backlight_task(void *arg) {
  int duty = BL_MIN_DUTY;
  int dir = BL_STEP;
  for (;;) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    duty += dir;
    if (duty >= BL_MAX_DUTY) {
      duty = BL_MAX_DUTY;
      dir = -BL_STEP;
    } else if (duty <= BL_MIN_DUTY) {
      duty = BL_MIN_DUTY;
      dir = BL_STEP;
    }
    vTaskDelay(pdMS_TO_TICKS(BL_STEP_MS));
  }
}

static void backlight_start(void) {
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = BL_PWM_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t ch = {
      .gpio_num = BL_PIN,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
      .duty = BL_MIN_DUTY,
      .hpoint = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch));

  xTaskCreate(backlight_task, "backlight", BL_TASK_STACK_BYTES, NULL,
              BL_TASK_PRIORITY, NULL);
  ESP_LOGI(TAG, "backlight pulse started");
}

/* ---------------- Haptics (DRV2605) ---------------- */

static void drv_write(uint8_t reg, uint8_t val) {
  if (!drv)
    return;
  uint8_t buf[2] = {reg, val};
  esp_err_t err = i2c_master_transmit(drv, buf, sizeof(buf), I2C_TIMEOUT_MS);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "drv2605 write failed: %s", esp_err_to_name(err));
}

static uint8_t drv_read(uint8_t reg) {
  uint8_t v = DRV_READ_FALLBACK;
  if (drv)
    i2c_master_transmit_receive(drv, &reg, sizeof(reg), &v, sizeof(v),
                                I2C_TIMEOUT_MS);
  return v;
}

static void drv_standby(void) { drv_write(REG_MODE, DRV_MODE_STANDBY); }
static void drv_rtp(void) { drv_write(REG_MODE, DRV_MODE_RTP); }

static void stop_cb(void *arg) {
  drv_write(REG_RTP_INPUT, 0);
  drv_standby();
}

static void haptic_init(void) {
  i2c_master_bus_config_t bus_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = I2C_GLITCH_IGNORE_CYCLES,
      .flags.enable_internal_pullup = true,
  };
  i2c_master_bus_handle_t bus;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

  esp_err_t pr = i2c_master_probe(bus, DRV2605_ADDR, I2C_TIMEOUT_MS);
  if (pr != ESP_OK) {
    ESP_LOGE(TAG, "DRV2605 not found: %s (haptics disabled)",
             esp_err_to_name(pr));
    return;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = DRV2605_ADDR,
      .scl_speed_hz = I2C_SCL_SPEED_HZ,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &drv));

  drv_write(REG_MODE, DRV_MODE_RESET); // device reset (comes up in standby)
  vTaskDelay(pdMS_TO_TICKS(DRV_RESET_DELAY_MS));
  drv_write(REG_CONTROL3, CONTROL3_VALUE);
  ESP_LOGI(TAG, "CONTROL3 = 0x%02X (expect 0x%02X)", drv_read(REG_CONTROL3),
           CONTROL3_VALUE);
  drv_write(REG_RTP_INPUT, 0);
  drv_standby(); // idle = off

  const esp_timer_create_args_t t = {.callback = stop_cb,
                                     .name = "haptic_stop"};
  ESP_ERROR_CHECK(esp_timer_create(&t, &stop_timer));
  ESP_LOGI(TAG, "haptics ready");
}

static void apply_haptic(int strength) {
  if (!drv || !stop_timer)
    return;
  if (strength < HAPTIC_STRENGTH_OFF)
    strength = HAPTIC_STRENGTH_OFF;
  if (strength > HAPTIC_STRENGTH_MAX)
    strength = HAPTIC_STRENGTH_MAX;
  ESP_LOGI(TAG, "haptic %d", strength);

  esp_timer_stop(stop_timer);
  if (strength == HAPTIC_STRENGTH_OFF) {
    drv_write(REG_RTP_INPUT, 0);
    drv_standby();
    return;
  }

  uint8_t amp =
      HAPTIC_MIN_AMP + (strength - HAPTIC_STRENGTH_MIN) *
                           (HAPTIC_MAX_AMP - HAPTIC_MIN_AMP) /
                           (HAPTIC_STRENGTH_MAX - HAPTIC_STRENGTH_MIN);
  // Match the watch's duration scaling, including its 1 ms minimum.
  uint64_t duration_us =
      (uint64_t)HAPTIC_PULSE_US * strength / HAPTIC_STRENGTH_MAX;
  if (duration_us < HAPTIC_MIN_PULSE_US)
    duration_us = HAPTIC_MIN_PULSE_US;
  drv_rtp();                               // wake into RTP mode
  drv_write(REG_CONTROL3, CONTROL3_VALUE); // re-assert unsigned + open loop
  drv_write(REG_RTP_INPUT, amp);
  ESP_LOGI(TAG, "amp=%u control3=0x%02X", amp, drv_read(REG_CONTROL3));
  esp_timer_start_once(stop_timer, duration_us);
}

/* ---------------- MQTT ---------------- */

static void handle_message(esp_mqtt_event_handle_t e) {
  char buf[MQTT_PAYLOAD_BUFFER_SIZE];
  if (e->data_len <= 0 || e->data_len >= (int)sizeof(buf)) {
    ESP_LOGW(TAG, "payload empty or too large (%d)", e->data_len);
    return;
  }
  memcpy(buf, e->data, e->data_len);
  buf[e->data_len] = '\0';

  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    ESP_LOGW(TAG, "invalid JSON: %s", buf);
    return;
  }

  cJSON *diff = cJSON_GetObjectItemCaseSensitive(root, "increaseDiff");
  if (cJSON_IsNumber(diff)) {
    apply_haptic(diff->valueint);
  } else {
    ESP_LOGW(TAG, "missing increaseDiff in: %s", buf);
  }
  cJSON_Delete(root);
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id,
                               void *data) {
  esp_mqtt_event_handle_t e = data;
  switch ((esp_mqtt_event_id_t)id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT connected");
    esp_mqtt_client_subscribe(client, TOPIC_RIGHT, MQTT_SUBSCRIBE_QOS);
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT disconnected (auto-reconnect)");
    break;
  case MQTT_EVENT_DATA:
    if (e->current_data_offset != 0 || e->data_len != e->total_data_len) {
      ESP_LOGW(TAG, "fragmented message ignored");
      break;
    }
    handle_message(e);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT error");
    break;
  default:
    break;
  }
}

static void mqtt_start(void) {
  esp_mqtt_client_config_t cfg = {
      .broker.address.uri = MQTT_URL,
      .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
      .credentials.username = MQTT_USER_AGENT_NAME,
      .credentials.authentication.password = MQTT_PASSWORD,
  };
  client = esp_mqtt_client_init(&cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 NULL);
  esp_mqtt_client_start(client);
}

/* ---------------- Wi-Fi ---------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "WiFi lost, reconnecting");
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP");
    if (!mqtt_started) {
      mqtt_started = true;
      mqtt_start();
    }
  }
}

void app_main(void) {
  esp_err_t r = nvs_flash_init();
  if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    r = nvs_flash_init();
  }
  ESP_ERROR_CHECK(r);

  backlight_start();
  haptic_init();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             wifi_event_handler, NULL));

  wifi_config_t wc = {0};
  strlcpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid));
  strlcpy((char *)wc.sta.password, WIFI_PASSWD, sizeof(wc.sta.password));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
}
