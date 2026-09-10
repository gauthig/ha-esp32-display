/*
 * main.c — HA Energy Display / Office Panel 7
 *
 * DEVICE_TYPE (from device_config.h) selects which UI and HA client compile:
 *   DEVICE_TYPE_ENERGY       → energy dashboard (ui.c + ha_client.c)
 *   DEVICE_TYPE_OFFICE_PANEL → 7B combo panel   (ui_office.c + ha_ham.c +
 *                              ha_light.c + ha_client.c + ha_history.c)
 *
 * Tasks:
 *   core 1 : LVGL task (via esp_lvgl_port, priority 4)
 *   core 0 : ha_poll_task (priority 5) — polls HA every HA_POLL_INTERVAL_MS
 *   core 0 : ha_hist_task (priority 3) — 7-day history prefetch (energy builds)
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
#elif DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL
#include "ha_ham.h"
#include "ha_light.h"
#include "ha_client.h"
#include "ha_history.h"
#include "ui_office.h"
#endif

static const char *TAG = "main";

/* --------------------------------------------------------- Poll task handle */
static TaskHandle_t  s_poll_task;

/* ============================================= History prefetch (energy) === */
#if defined(HAS_ENERGY)

/*
 * Cached history updated by ha_hist_task every 10 minutes.
 * Written under lvgl_port_lock; read by the chart-request callback which is
 * already called under the LVGL lock from an event callback.
 */
static ha_history_t s_hist_cache;

static void on_chart_requested(void)
{
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
    ui_show_chart(&s_hist_cache);
#elif DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL
    ui_office_show_chart(&s_hist_cache);
#endif
}

static void ha_hist_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(8000));   /* let SNTP sync first */

    while (true) {
        ha_history_t tmp = {};
        if (ha_history_fetch_combined(&tmp) == ESP_OK) {
            if (lvgl_port_lock(500)) {
                s_hist_cache = tmp;
                lvgl_port_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10UL * 60UL * 1000UL));
    }
}

#endif /* HAS_ENERGY */

/* =================================================== Office-panel state ==== */
#if DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL

/* All set under the LVGL lock from widget callbacks; read + cleared by the
 * poll task on core 0. */
static volatile int      s_ham_toggle_request = -1;   /* switch idx, or -1   */
static volatile int      s_light_req          = 0;    /* 0 none 1 tgl 2 bri 3 rgb */
static volatile int      s_light_bri_pct      = 0;
static volatile uint32_t s_light_rgb          = 0;    /* 0x00RRGGBB         */

static void on_ham_toggle(int switch_idx)
{
    s_ham_toggle_request = switch_idx;
    xTaskNotifyGive(s_poll_task);
}
static void on_light_toggle(void)
{
    s_light_req = 1;
    xTaskNotifyGive(s_poll_task);
}
static void on_light_brightness(int percent)
{
    s_light_bri_pct = percent;
    s_light_req = 2;
    xTaskNotifyGive(s_poll_task);
}
static void on_light_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    s_light_rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    s_light_req = 3;
    xTaskNotifyGive(s_poll_task);
}

#endif /* DEVICE_TYPE_OFFICE_PANEL */

/* ----------------------------------------------------------------- WiFi */

static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONN_BIT BIT0

static void set_ui_connected(bool connected)
{
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
    ui_set_connected(connected);
#elif DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL
    ui_office_set_connected(connected);
#endif
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected — reconnecting");
        if (lvgl_port_lock(0)) { set_ui_connected(false); lvgl_port_unlock(); }
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
    (void)arg;

#if DEVICE_TYPE == DEVICE_TYPE_ENERGY

    ha_data_t data = {};
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
        } else if (lvgl_port_lock(0)) {
            ui_set_connected(false);
            lvgl_port_unlock();
        }
    }

#elif DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL

    ha_ham_data_t   ham   = {};
    ha_light_data_t light = {};
    ha_data_t       energy = {};

    bool ok_ham   = (ha_ham_fetch(&ham)     == ESP_OK);
    bool ok_light = (ha_light_fetch(&light) == ESP_OK);
    bool ok_energy = (ha_client_fetch(&energy) == ESP_OK);
    if (lvgl_port_lock(200)) {
        ui_office_set_connected(ok_ham || ok_light || ok_energy);
        if (ok_ham)    ui_office_update_ham(&ham);
        if (ok_light)  ui_office_update_light(&light);
        if (ok_energy) ui_office_update_energy(&energy);
        lvgl_port_unlock();
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HA_POLL_INTERVAL_MS));

        /* ---- service pending control requests ---- */
        int ham_req = s_ham_toggle_request;
        if (ham_req >= 0) {
            s_ham_toggle_request = -1;
            ha_ham_toggle(ham_req);
            vTaskDelay(pdMS_TO_TICKS(400));
        }

        int lreq = s_light_req;
        if (lreq != 0) {
            s_light_req = 0;
            if (lreq == 1) {
                ha_light_toggle();
            } else if (lreq == 2) {
                ha_light_set_brightness(s_light_bri_pct);
            } else if (lreq == 3) {
                uint32_t c = s_light_rgb;
                ha_light_set_rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            }
            vTaskDelay(pdMS_TO_TICKS(400));
        }

        /* ---- refetch + push ---- */
        ok_ham    = (ha_ham_fetch(&ham)       == ESP_OK);
        ok_light  = (ha_light_fetch(&light)   == ESP_OK);
        ok_energy = (ha_client_fetch(&energy) == ESP_OK);

        if (lvgl_port_lock(200)) {
            ui_office_set_connected(ok_ham || ok_light || ok_energy);
            if (ok_ham)    ui_office_update_ham(&ham);
            if (ok_light)  ui_office_update_light(&light);
            if (ok_energy) ui_office_update_energy(&energy);
            lvgl_port_unlock();
        }
    }

#endif /* DEVICE_TYPE */
}

/* ----------------------------------------------------------------- Main */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(board_display_init());

    /* Build the UI shell before WiFi — shows "connecting" state */
    if (lvgl_port_lock(portMAX_DELAY)) {
#if DEVICE_TYPE == DEVICE_TYPE_ENERGY
        ui_set_chart_request_cb(on_chart_requested);
        ui_init();
#elif DEVICE_TYPE == DEVICE_TYPE_OFFICE_PANEL
        ui_office_set_ham_toggle_cb(on_ham_toggle);
        ui_office_set_light_toggle_cb(on_light_toggle);
        ui_office_set_light_brightness_cb(on_light_brightness);
        ui_office_set_light_rgb_cb(on_light_rgb);
        ui_office_set_chart_request_cb(on_chart_requested);
        ui_office_init();
#endif
        lvgl_port_unlock();
    }

    wifi_init();
    sntp_start();

    xTaskCreatePinnedToCore(ha_poll_task, "ha_poll", 8192, NULL, 5,
                            &s_poll_task, 0);

#if defined(HAS_ENERGY)
    xTaskCreatePinnedToCore(ha_hist_task, "ha_hist", 8192, NULL, 3, NULL, 0);
#endif

    ESP_LOGI(TAG, "startup complete — device: %s", DEVICE_NAME);
}
