#include "cJSON.h"
#include "driver/ledc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "haptics.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "wifi_roulette.h"
#include <stdio.h>
#include <string.h>

#define BL_PIN 45        // T-Watch S3 display backlight
#define BL_MIN_DUTY 30   // dimmest point of the pulse (0..1023)
#define BL_MAX_DUTY 1023 // brightest point of the pulse
#define BL_STEP 16
#define BL_STEP_MS 20 // full dim-to-bright sweep is about 1.2 s
#define BL_PWM_FREQ_HZ 5000
#define BL_TASK_STACK_BYTES 2048
#define BL_TASK_PRIORITY 1

#define MQTT_PAYLOAD_BUFFER_SIZE 1024 // includes the LogData envelope and NUL
#define MQTT_SUBSCRIBE_QOS 0        // at-most-once delivery

static const char *TAG = "mqtt_sub";

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

  cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
  cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag");
  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsString(device) || !cJSON_IsString(tag) ||
      !cJSON_IsString(data)) {
    ESP_LOGW(TAG, "invalid LogData envelope");
    cJSON_Delete(root);
    return;
  }

  cJSON *payload = cJSON_Parse(data->valuestring);
  if (!payload) {
    ESP_LOGW(TAG, "invalid JSON in LogData.data");
    cJSON_Delete(root);
    return;
  }

  cJSON *diff = cJSON_GetObjectItemCaseSensitive(payload, "increaseDiff");
  if (cJSON_IsNumber(diff)) {
    apply_haptic(diff->valueint);
  } else {
    ESP_LOGW(TAG, "missing increaseDiff in LogData.data: %s", data->valuestring);
  }
  cJSON_Delete(payload);
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
    ESP_LOGI(TAG, "MQTT received: topic=%.*s payload=%.*s", e->topic_len,
             e->topic ? e->topic : "", e->data_len, e->data ? e->data : "");
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

static void wifi_connected(void) {
  if (!mqtt_started) {
    mqtt_started = true;
    mqtt_start();
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

  wifi_roulette_start(wifi_connected);
}
