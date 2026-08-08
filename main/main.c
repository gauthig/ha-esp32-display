/*
 * main.c — HA Energy Display / Ham Radio Control Panel
 *
 * DEVICE_TYPE (from device_config.h) selects which UI and HA client compile:
 *   DEVICE_TYPE_ENERGY       → energy dashboard (ui.c + ha_client.c)
 *   DEVICE_TYPE_HAM_CONTROLS → ham radio panel  (ui_ham.c + ha_ham.c)
 *
 * Tasks:
 *   core 1 : LVGL task (via esp_lvgl_port, priority 4)
 *   core 0 : ha_poll_task (priority 5) — polls HA every HA_POLL_INTERVAL_MS
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

#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
#include "ha_client.h"
#include "ha_history.h"
#include "ui.h"
#elif DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS
#include "ha_ham.h"
#include "ui_ham.h"
#endif

static const char *TAG = "main";

/* --------------------------------------------------------- Poll task handle */
static TaskHandle_t  s_poll_task;

/* ====================================================== Energy-only state == */
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY

/*
 * Chart request: set from LVGL task (event callback, under lvgl lock).
 * Read and cleared by ha_poll_task on core 0.
 * -1 = no request; 0 = HIST_GRID; 1 = HIST_SOLAR.
 */
static volatile int s_chart_request = -1;

static void on_chart_requested(int type)
{
    s_chart_request = type;
    xTaskNotifyGive(s_poll_task);
}

#endif /* DEVICE_TYPE_ENERGY */

/* ====================================================== Ham-only state ===== */
#if DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS

/*
 * Toggle request: set from LVGL task (button callback, under lvgl lock).
 * Read and cleared by ha_poll_task on core 0.
 * -1 = no request; 0/1/2 = switch index to toggle.
 */
static volatile int s_toggle_request = -1;

static void on_toggle_requested(int switch_idx)
{
    s_toggle_request = switch_idx;
    xTaskNotifyGive(s_poll_task);
}

#endif /* DEVICE_TYPE_HAM_CONTROLS */

/* ----------------------------------------------------------------- WiFi */

static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONN_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected — reconnecting");
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
        if (lvgl_port_lock(0)) { ui_set_connected(false);     lvgl_port_unlock(); }
#elif DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS
        if (lvgl_port_lock(0)) { ui_ham_set_connected(false); lvgl_port_unlock(); }
#endif
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

    setenv("TZ", LOCAL_TZ, 1);
    tzset();

    ESP_LOGI(TAG, "SNTP started (timezone: %s)", LOCAL_TZ);
}

/* ------------------------------------------------------------- HA poll */

static void ha_poll_task(void *arg)
{
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY

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
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HA_POLL_INTERVAL_MS));

        int chart_req = s_chart_request;
        if (chart_req >= 0) {
            s_chart_request = -1;
            ha_history_t hist = {};
            ha_history_fetch((ha_history_type_t)chart_req, &hist);
            if (lvgl_port_lock(500)) {
                ui_show_chart((ha_history_type_t)chart_req, &hist);
                lvgl_port_unlock();
            }
            continue;
        }

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

#elif DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS

    ha_ham_data_t data = {};

    /* First fetch immediately */
    if (ha_ham_fetch(&data) == ESP_OK) {
        if (lvgl_port_lock(200)) {
            ui_ham_set_connected(true);
            ui_ham_update(&data);
            lvgl_port_unlock();
        }
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HA_POLL_INTERVAL_MS));

        int toggle_req = s_toggle_request;
        if (toggle_req >= 0) {
            s_toggle_request = -1;
            /* Toggle the switch then immediately re-fetch to update state */
            ha_ham_toggle(toggle_req);
            vTaskDelay(pdMS_TO_TICKS(500));   /* brief pause for HA to process */
        }

        if (ha_ham_fetch(&data) == ESP_OK) {
            if (lvgl_port_lock(200)) {
                ui_ham_set_connected(true);
                ui_ham_update(&data);
                lvgl_port_unlock();
            }
        } else {
            if (lvgl_port_lock(0)) {
                ui_ham_set_connected(false);
                lvgl_port_unlock();
            }
        }
    }

#endif /* DEVICE_TYPE */
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
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
        ui_set_chart_request_cb(on_chart_requested);
        ui_init();
#elif DEVICE_TYPE == DEVICE_TYPE_HAM_CONTROLS
        ui_ham_set_toggle_cb(on_toggle_requested);
        ui_ham_init();
#endif
        lvgl_port_unlock();
    }

    /* WiFi + SNTP */
    wifi_init();
    sntp_start();

    /* Poll task on core 0; save handle for wake-up notifications */
    xTaskCreatePinnedToCore(ha_poll_task, "ha_poll", 8192, NULL, 5,
                            &s_poll_task, 0);

    ESP_LOGI(TAG, "startup complete — device: %s", DEVICE_NAME);
}
