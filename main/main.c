/*
 * main.c — HA Energy Display
 *
 * Tasks:
 *   core 1 : LVGL task (via esp_lvgl_port, priority 4)
 *   core 0 : ha_poll_task (priority 5) — polls HA every 30 s
 *
 * Startup sequence:
 *   1. NVS + netif
 *   2. board_display_init → UI shell visible immediately
 *   3. WiFi connect (blocks until IP obtained)
 *   4. SNTP start (async; clock may lag a few seconds)
 *   5. ha_poll_task starts
 */
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "board.h"
#include "ha_config.h"
#include "ha_client.h"
#include "ui.h"

static const char *TAG = "main";

/* ----------------------------------------------------------------- WiFi */

static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONN_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected — reconnecting");
        if (lvgl_port_lock(0)) {
            ui_set_connected(false);
            lvgl_port_unlock();
        }
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_eg, WIFI_CONN_BIT);
    }
}

static void wifi_init(void)
{
    s_wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wcfg = {};
    strlcpy((char *)wcfg.sta.ssid,     WIFI_SSID,     sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, WIFI_PASSWORD, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "Waiting for WiFi...");
    xEventGroupWaitBits(s_wifi_eg, WIFI_CONN_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected");
}

/* ----------------------------------------------------------------- SNTP */

static void sntp_start(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    /* Set timezone */
    setenv("TZ", LOCAL_TZ, 1);
    tzset();

    ESP_LOGI(TAG, "SNTP started (timezone: %s)", LOCAL_TZ);
}

/* ------------------------------------------------------------- HA poll */

static void ha_poll_task(void *arg)
{
    ha_data_t data = {};

    /* First fetch immediately */
    if (ha_client_fetch(&data) == ESP_OK) {
        if (lvgl_port_lock(200)) {
            ui_set_connected(true);
            ui_update(&data);
            lvgl_port_unlock();
        }
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HA_POLL_INTERVAL_MS));

        if (ha_client_fetch(&data) == ESP_OK) {
            if (lvgl_port_lock(200)) {
                ui_set_connected(true);
                ui_update(&data);
                lvgl_port_unlock();
            }
        } else {
            if (lvgl_port_lock(0)) {
                ui_set_connected(false);
                lvgl_port_unlock();
            }
        }
    }
}

/* ----------------------------------------------------------------- Main */

void app_main(void)
{
    /* NVS — required by WiFi driver */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Display + touch + LVGL (core 1) */
    ESP_ERROR_CHECK(board_display_init());

    /* Build the UI shell before WiFi — shows "connecting" state */
    if (lvgl_port_lock(portMAX_DELAY)) {
        ui_init();
        lvgl_port_unlock();
    }

    /* WiFi + SNTP */
    wifi_init();
    sntp_start();

    /* Poll task on core 0 */
    xTaskCreatePinnedToCore(ha_poll_task, "ha_poll", 8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "startup complete");
}
