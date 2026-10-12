#include "wifi_roulette.h"
#include "wifi_networks.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RETRY_DELAY_MS 1000
#define WIFI_TASK_STACK_BYTES 4096
#define WIFI_TASK_PRIORITY 3
#define WIFI_STARTED (1U << 0)
#define WIFI_DISCONNECTED (1U << 1)
#define WIFI_CONNECTED (1U << 2)
#define WIFI_NETWORK_COUNT (sizeof(wifi_networks) / sizeof(wifi_networks[0]))

static const char *TAG = "wifi_roulette";
static TaskHandle_t wifi_task;
static void (*connected_callback)(void);

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    xTaskNotify(wifi_task, WIFI_STARTED, eSetBits);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *event = data;
    ESP_LOGW(TAG, "Disconnected (reason %u)", event->reason);
    xTaskNotify(wifi_task, WIFI_DISCONNECTED, eSetBits);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP");
    xTaskNotify(wifi_task, WIFI_CONNECTED, eSetBits);
    if (connected_callback)
      connected_callback();
  }
}

static void wifi_connect_task(void *arg) {
  uint32_t events;
  xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);

  size_t index = 0;
  for (;;) {
    wifi_config_t config = {0};
    size_t ssid_len = strlen(wifi_networks[index].ssid);
    size_t password_len = strlen(wifi_networks[index].password);
    // The driver accepts full-length SSIDs/passwords without a trailing NUL.
    memcpy(config.sta.ssid, wifi_networks[index].ssid, ssid_len);
    memcpy(config.sta.password, wifi_networks[index].password, password_len);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_LOGI(TAG, "Trying network %u/%u: %s", (unsigned)(index + 1),
             (unsigned)WIFI_NETWORK_COUNT, wifi_networks[index].ssid);

    esp_err_t err = esp_wifi_connect();
    if (err == ESP_OK) {
      events = 0;
      xTaskNotifyWait(0, UINT32_MAX, &events,
                      pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
      if ((events & WIFI_CONNECTED) && !(events & WIFI_DISCONNECTED)) {
        // Stay on a working network until it disconnects.
        do {
          xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);
        } while (!(events & WIFI_DISCONNECTED));
      } else if (!(events & WIFI_DISCONNECTED)) {
        ESP_LOGW(TAG, "Connection timed out");
        err = esp_wifi_disconnect();
        if (err == ESP_OK) {
          // Consume the intentional disconnect before configuring the next AP.
          do {
            xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);
          } while (!(events & WIFI_DISCONNECTED));
        } else {
          ESP_LOGW(TAG, "Disconnect failed: %s", esp_err_to_name(err));
        }
      }
    } else {
      ESP_LOGW(TAG, "Connect failed: %s", esp_err_to_name(err));
    }

    index = (index + 1) % WIFI_NETWORK_COUNT;
    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
  }
}

void wifi_roulette_start(void (*on_connected)(void)) {
  // Reject invalid entries rather than silently truncating credentials.
  for (size_t i = 0; i < WIFI_NETWORK_COUNT; ++i) {
    size_t ssid_len = strlen(wifi_networks[i].ssid);
    size_t password_len = strlen(wifi_networks[i].password);
    if (ssid_len == 0 || ssid_len > 32 || password_len > 64) {
      ESP_LOGE(TAG, "Invalid credentials at network %u", (unsigned)(i + 1));
      ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
  }

  connected_callback = on_connected;
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  if (!esp_netif_create_default_wifi_sta())
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             wifi_event_handler, NULL));
  if (xTaskCreate(wifi_connect_task, "wifi_roulette", WIFI_TASK_STACK_BYTES, NULL,
                  WIFI_TASK_PRIORITY, &wifi_task) != pdPASS)
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(esp_wifi_start());
}
