#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "keyboard.h"
#include "display.h"

static const char *TAG = "RHEMA_WRITER";

#define MAX_BUFFER_SIZE 512
static char text_buffer[MAX_BUFFER_SIZE] = "RHEMA WRITER v1.0\nReady for input...\n\n";

// Task to read keystrokes from the queue and update the display
static void input_processing_task(void *pvParameters)
{
    char c;
    while (1) {
        // Wait for characters from the Bluetooth keyboard queue
        if (xQueueReceive(keyboard_char_queue, &c, portMAX_DELAY) == pdTRUE) {
            size_t len = strlen(text_buffer);

            if (c == '\b') { // Backspace
                if (len > 0) {
                    text_buffer[len - 1] = '\0';
                }
            } else {
                // Append character if buffer has room
                if (len < MAX_BUFFER_SIZE - 2) {
                    text_buffer[len] = c;
                    text_buffer[len + 1] = '\0';
                } else {
                    // Buffer full: shift content or reset for now
                    memmove(text_buffer, text_buffer + 50, len - 50 + 1);
                    len = strlen(text_buffer);
                    text_buffer[len] = c;
                    text_buffer[len + 1] = '\0';
                }
            }

            // Update the screen with the current text buffer
            display_set_text(text_buffer);
        }
    }
}

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

    // Initialize Display & LVGL UI
    display_init();

    // Initialize Bluetooth Keyboard Module (starts background scan)
    keyboard_init();

    // Spawn task to process keyboard queue and drive the text buffer
    xTaskCreate(input_processing_task, "input_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Initialization complete. Turn on your foldable keyboard!");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}