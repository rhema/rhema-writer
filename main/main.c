// Next up: https://gemini.google.com/app/ba811392a334991e
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h" // Required for Bluetooth bonding

#include "keyboard.h"
// #include "input/input.h"
// #include "display.h"
// #include "editor.h"
// #include "storage.h"
// #include "system.h"

static const char *TAG = "RHEMA_WRITER";

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Rhema Writer - Keyboard Isolation Test");
    ESP_LOGI(TAG, "========================================");

    // 1. Initialize NVS (Core requirement for Bluetooth)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // system_init();
    // storage_init();
    // display_init();
    // input_init();
    // editor_init();

    // 2. Start the Bluetooth module
    keyboard_init();

    ESP_LOGI(TAG, "Initialization complete. Waiting for keyboard connection...");

    while (1) {
        // keyboard_process(); // Background BT tasks handle this automatically
        // editor_process();
        // display_process();

        // 3. Prove the Queue works by pulling characters out of it
        char typed_char;
        if (keyboard_char_queue != NULL && xQueueReceive(keyboard_char_queue, &typed_char, 0) == pdTRUE) {
            
            // Print the typed character directly to the VS Code terminal
            if (typed_char == '\n') {
                printf("⌨️ TYPED: [ENTER]\n");
            } else if (typed_char == '\b') {
                printf("⌨️ TYPED: [BACKSPACE]\n");
            } else {
                printf("⌨️ TYPED: %c\n", typed_char);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}