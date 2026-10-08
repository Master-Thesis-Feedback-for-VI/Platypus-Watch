#include "cJSON.h"
#include "driver/i2c_master.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <stdio.h>

#define I2C_SDA_PIN 10
#define I2C_SCL_PIN 11
#define DRV2605_ADDR 0x5A
#define REG_MODE 0x01
#define REG_RTP_INPUT 0x02
#define REG_CONTROL3 0x1D
#define HAPTIC_MIN_AMP                                                         \
  90 // lowest amplitude that still feels like a buzz, tune this
#define HAPTIC_PULSE_US 200000 // auto-stop 200 ms after the last message

static i2c_master_dev_handle_t drv;
static esp_timer_handle_t stop_timer;

static const char *TAG = "mqtt_sub";
static esp_mqtt_client_handle_t client;
static bool mqtt_started = false;

static void drv_write(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  esp_err_t err = i2c_master_transmit(drv, buf, sizeof(buf), 100);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "drv2605 write failed: %s", esp_err_to_name(err));
}

static void stop_cb(void *arg) { drv_write(REG_RTP_INPUT, 0); }

static void haptic_init(void) {
  i2c_master_bus_config_t bus_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  i2c_master_bus_handle_t bus;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

  ESP_ERROR_CHECK(
      i2c_master_probe(bus, DRV2605_ADDR, 100)); // fails if powered off

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = DRV2605_ADDR,
      .scl_speed_hz = 400000,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &drv));

  drv_write(REG_CONTROL3, 0x28); // ERM open loop + unsigned RTP data
  drv_write(REG_RTP_INPUT, 0);
  drv_write(REG_MODE, 0x05); // leave standby, enter RTP mode

  const esp_timer_create_args_t t = {.callback = stop_cb,
                                     .name = "haptic_stop"};
  ESP_ERROR_CHECK(esp_timer_create(&t, &stop_timer));
}

static void apply_haptic(int strength) {
  if (strength < 0)
    strength = 0;
  if (strength > 10)
    strength = 10;
  ESP_LOGI(TAG, "haptic %d", strength);

  esp_timer_stop(stop_timer);
  if (strength == 0) {
    drv_write(REG_RTP_INPUT, 0);
    return;
  }

  uint8_t amp = HAPTIC_MIN_AMP + (strength - 1) * (255 - HAPTIC_MIN_AMP) / 9;
  drv_write(REG_RTP_INPUT, amp);
  esp_timer_start_once(stop_timer, HAPTIC_PULSE_US);
}

static void handle_message(esp_mqtt_event_handle_t e) {
  char buf[64];
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
    esp_mqtt_client_subscribe(client, TOPIC_LEFT, 0);
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
  haptic_init()
}
