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

// Typematic Repeat State
static uint8_t held_keycode = 0;
static uint8_t held_modifiers = 0;
static TickType_t press_time = 0;
static TickType_t last_repeat_time = 0;

void input_init(void) {
    ESP_LOGI(TAG, "Initializing Input Module...");
}

static bool was_pressed(uint8_t code, const uint8_t *keys) {
    for (int i = 0; i < 6; i++) {
        if (keys[i] == code) return true;
    }
    return false;
}

static bool decode_key(uint8_t code, uint8_t modifiers, input_event_t *event) {
    bool ctrl = (modifiers & 0x11) != 0;
    bool shifted = (modifiers & 0x22) != 0;

    event->key = INPUT_KEY_NONE;
    event->character = '\0';

    if (ctrl) {
        if (code == 0x16) { event->key = INPUT_KEY_CTRL_S; return true; }
        if (code == 0x12) { event->key = INPUT_KEY_CTRL_O; return true; }
        if (code == 0x0C) { event->key = INPUT_KEY_CTRL_I; return true; }
    }

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
            bool upper = shifted ^ caps_lock;
            if (code >= 0x04 && code <= 0x1D) {
                char c = 'a' + (code - 0x04);
                if (upper) c = toupper((unsigned char)c);
                event->key = INPUT_KEY_CHAR;
                event->character = c;
            } else if (code >= 0x1E && code <= 0x27) {
                char num_chars[] = "!@#$%^&*()";
                char base_chars[] = "1234567890";
                int idx = code - 0x1E;
                event->key = INPUT_KEY_CHAR;
                event->character = shifted ? num_chars[idx] : base_chars[idx];
            } 
            // Missing Punctuation & Symbols
            else if (code == 0x2C) { event->key = INPUT_KEY_CHAR; event->character = ' '; }
            else if (code == 0x2D) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '_' : '-'; }
            else if (code == 0x2E) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '+' : '='; }
            else if (code == 0x2F) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '{' : '['; }
            else if (code == 0x30) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '}' : ']'; }
            else if (code == 0x31) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '|' : '\\'; }
            else if (code == 0x33) { event->key = INPUT_KEY_CHAR; event->character = shifted ? ':' : ';'; }
            else if (code == 0x34) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '"' : '\''; }
            else if (code == 0x35) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '~' : '`'; }
            else if (code == 0x36) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '<' : ','; }
            else if (code == 0x37) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '>' : '.'; }
            else if (code == 0x38) { event->key = INPUT_KEY_CHAR; event->character = shifted ? '?' : '/'; }
            break;
        }
    }

    return event->key != INPUT_KEY_NONE;
}

bool input_get_event(input_event_t *event) {
    if (event == NULL || keyboard_char_queue == NULL) return false;

    uint8_t report[8];
    
    // Instead of blocking forever, wait 30ms to allow for key repeat processing
    if (xQueueReceive(keyboard_char_queue, report, pdMS_TO_TICKS(30)) == pdTRUE) {
        uint8_t modifiers = report[0];
        const uint8_t *keys = &report[2];
        
        uint8_t new_key = 0;
        for (int i = 0; i < 6; i++) {
            if (keys[i] != 0 && !was_pressed(keys[i], previous_keys)) {
                new_key = keys[i];
                break;
            }
        }

        if (new_key != 0) {
            if (new_key == 0x39) { // Caps Lock is a toggle, do not autorepeat
                caps_lock = !caps_lock;
                held_keycode = 0;
            } else {
                held_keycode = new_key;
                held_modifiers = modifiers;
                press_time = xTaskGetTickCount();
                last_repeat_time = 0;
            }
        } else {
            // Check if the currently held key was released
            if (held_keycode != 0 && !was_pressed(held_keycode, keys)) {
                held_keycode = 0; // Released
            } else if (held_keycode != 0) {
                held_modifiers = modifiers; // Update modifiers (e.g. shift released while key held)
            }
        }

        memcpy(previous_keys, keys, 6);

        if (new_key != 0 && new_key != 0x39) {
            return decode_key(new_key, held_modifiers, event);
        }
    } else {
        // Timeout triggered: Check for auto-repeat
        if (held_keycode != 0 && held_keycode != 0x39) {
            TickType_t now = xTaskGetTickCount();
            if ((now - press_time) > pdMS_TO_TICKS(500)) {       // 500ms initial press delay
                if ((now - last_repeat_time) > pdMS_TO_TICKS(40)) { // 40ms repeat interval
                    last_repeat_time = now;
                    return decode_key(held_keycode, held_modifiers, event);
                }
            }
        }
    }
    return false;
}