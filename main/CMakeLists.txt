idf_component_register(
    SRCS "main.c" "board.c" "ha_client.c" "ha_history.c" "ui.c"
         "ha_ham.c" "ui_ham.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi esp_event nvs_flash esp_netif esp_http_client
        driver esp_lcd esp_timer app_update lwip json
        espressif__esp_lcd_touch_gt911
        espressif__esp_lvgl_port
        lvgl__lvgl
)
