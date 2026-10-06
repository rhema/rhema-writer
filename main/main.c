#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "keyboard.h"
#include "display.h"
#include "input.h"
#include "editor.h"

static const char *TAG = "RHEMA_WRITER";

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting Rhema Writer");
    ESP_LOGI(TAG, "========================================");

    // Initialize Non-Volatile Storage (required for BLE bonding)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Display & LVGL UI (starts with a clean slate)
    display_init();

    // Initialize Input Module
    input_init();

    // Initialize Bluetooth Keyboard Module (starts background scan & auto-reconnect)
    keyboard_init();

    // Initialize Kilo Editor Task (takes over screen rendering and key processing)
    editor_init();

    ESP_LOGI(TAG, "Initialization complete. System running.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}