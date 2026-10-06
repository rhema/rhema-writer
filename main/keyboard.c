#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidh.h"

#include "keyboard.h"

static const char *TAG = "KEYBOARD";

QueueHandle_t keyboard_char_queue = NULL;

static volatile bool target_ready_to_connect = false;
static volatile bool is_connected = false;
static esp_bd_addr_t target_bda;
static esp_ble_addr_type_t target_addr_type;

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

/* -----------------------------------------------------------
 * HID Event Callback
 * --------------------------------------------------------- */
void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
        case ESP_HIDH_OPEN_EVENT:
            if (param->open.status == ESP_OK) {
                is_connected = true;
                ESP_LOGI(TAG, "✅ BLE Keyboard Connected Successfully!");
            } else {
                is_connected = false;
                ESP_LOGE(TAG, "❌ OPEN failed! status=0x%x. Retrying scan in 2s...", param->open.status);
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_ble_gap_start_scanning(10);
            }
            break;

        case ESP_HIDH_INPUT_EVENT:
            if (param->input.usage == ESP_HID_USAGE_KEYBOARD && param->input.length >= 8) {
                // Push raw 8-byte report into the queue
                xQueueSend(keyboard_char_queue, param->input.data, 0);
            }
            break;

        case ESP_HIDH_CLOSE_EVENT:
            is_connected = false;
            ESP_LOGW(TAG, "⚠️ Keyboard Disconnected. Stabilizing and rescanning...");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Allow stack to completely clear connection handle
            target_ready_to_connect = false;
            esp_ble_gap_start_scanning(10);
            break;

        default:
            break;
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Starting BLE scan for keyboard...");
            esp_ble_gap_start_scanning(10);
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
            if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT && !target_ready_to_connect && !is_connected) {
                uint8_t adv_name_len = 0;
                uint8_t *adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
                
                if (adv_name_len > 0) {
                    char name_buf[32] = {0};
                    int copy_len = adv_name_len < 31 ? adv_name_len : 31;
                    memcpy(name_buf, adv_name, copy_len);

                    if (strstr(name_buf, "Foldable Keyboard") != NULL) {
                        ESP_LOGI(TAG, "🎯 Target found (%s)! Stopping scan...", name_buf);
                        memcpy(target_bda, scan_result->scan_rst.bda, sizeof(esp_bd_addr_t));
                        target_addr_type = scan_result->scan_rst.ble_addr_type;
                        target_ready_to_connect = true;
                        esp_ble_gap_stop_scanning();
                    }
                }
            }
            break;
        }

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if (!is_connected && target_ready_to_connect) {
                // Task will handle opening
            } else if (!is_connected) {
                // If scan timed out without finding target, restart scan
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_ble_gap_start_scanning(10);
            }
            break;

        default:
            break;
    }
}

static void keyboard_connect_task(void *pvParameters) {
    while (1) {
        if (target_ready_to_connect && !is_connected) {
            vTaskDelay(pdMS_TO_TICKS(500)); // Let scan stop settle completely
            ESP_LOGI(TAG, "Opening HID connection safely from task context...");
            esp_err_t err = esp_hidh_dev_open(target_bda, ESP_HID_TRANSPORT_BLE, target_addr_type);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open device: %s. Rescanning...", esp_err_to_name(err));
                target_ready_to_connect = false;
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_ble_gap_start_scanning(10);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void keyboard_init(void) {
    ESP_LOGI(TAG, "Initializing Bluetooth Keyboard Module...");

    keyboard_char_queue = xQueueCreate(32, 8);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(esp_gap_cb));

    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    esp_hidh_config_t config = {
        .callback = hidh_callback,
        .event_stack_size = 8192,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&config));

    xTaskCreate(&keyboard_connect_task, "kb_conn_task", 4096, NULL, 3, NULL);

    esp_ble_gap_set_scan_params(&ble_scan_params);
}

void keyboard_process(void) {}