#include "input.h"
#include "keyboard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "INPUT";

static uint8_t previous_keys[6] = {0};
static bool caps_lock = false;

void input_init(void)
{
    ESP_LOGI(TAG, "Initializing Input Module...");
}

bool input_get_event(input_event_t *event)
{
    if (event == NULL || keyboard_char_queue == NULL) {
        return false;
    }

    uint8_t report[8];
    // Block until a new HID report arrives from the queue
    if (xQueueReceive(keyboard_char_queue, report, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    event->key = INPUT_KEY_NONE;
    event->character = '\0';

    uint8_t modifiers = report[0];
    const uint8_t *keys = &report[2];

    bool ctrl = (modifiers & 0x11) != 0; // Left or Right Ctrl
    bool shifted = (modifiers & 0x22) != 0; // Left or Right Shift

    // Find newly pressed key
    for (int i = 0; i < 6; i++) {
        uint8_t code = keys[i];
        if (code == 0) continue;

        // Check if key was already held down in previous report
        bool already_pressed = false;
        for (int p = 0; p < 6; p++) {
            if (previous_keys[p] == code) {
                already_pressed = true;
                break;
            }
        }

        if (!already_pressed) {
            // Handle Caps Lock toggle
            if (code == 0x39) {
                caps_lock = !caps_lock;
                continue;
            }

            // Handle Control Shortcuts
            if (ctrl) {
                if (code == 0x16) { event->key = INPUT_KEY_CTRL_S; memcpy(previous_keys, keys, 6); return true; } // S
                if (code == 0x12) { event->key = INPUT_KEY_CTRL_O; memcpy(previous_keys, keys, 6); return true; } // O
                if (code == 0x0C) { event->key = INPUT_KEY_CTRL_I; memcpy(previous_keys, keys, 6); return true; } // I
            }

            // Handle Special Navigation / Editing Keys
            switch (code) {
                case 0x28: event->key = INPUT_KEY_ENTER; event->character = '\n'; break;
                case 0x2A: event->key = INPUT_KEY_BACKSPACE; event->character = '\b'; break;
                case 0x4C: event->key = INPUT_KEY_DELETE; break;
                case 0x2B: event->key = INPUT_KEY_TAB; event->character = '\t'; break;
                case 0x29: event->key = INPUT_KEY_ESCAPE; break;
                case 0x4F: event->key = INPUT_KEY_RIGHT; break;
                case 0x50: event->key = INPUT_KEY_LEFT; break;
                case 0x52: event->key = INPUT_KEY_UP; break;
                case 0x51: event->key = INPUT_KEY_DOWN; break;
                case 0x4A: event->key = INPUT_KEY_HOME; break;
                case 0x4D: event->key = INPUT_KEY_END; break;
                default: {
                    // Printable characters (A-Z, numbers, symbols)
                    bool upper = shifted ^ caps_lock;
                    if (code >= 0x04 && code <= 0x1D) {
                        char c = 'a' + (code - 0x04);
                        if (upper) c = toupper(c);
                        event->key = INPUT_KEY_CHAR;
                        event->character = c;
                    } else if (code >= 0x1E && code <= 0x27) {
                        // Number row
                        char num_chars[] = "!@#$%^&*()";
                        char base_chars[] = "1234567890";
                        int idx = code - 0x1E;
                        event->key = INPUT_KEY_CHAR;
                        event->character = shifted ? num_chars[idx] : base_chars[idx];
                    } else if (code == 0x2C) {
                        event->key = INPUT_KEY_CHAR;
                        event->character = ' ';
                    }
                    break;
                }
            }

            memcpy(previous_keys, keys, 6);
            if (event->key != INPUT_KEY_NONE) {
                return true;
            }
        }
    }

    memcpy(previous_keys, keys, 6);
    return false;
}