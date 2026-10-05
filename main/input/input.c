#include "input.h"
#include <stddef.h>
#include "esp_log.h"

static const char *TAG = "INPUT";

void input_init(void)
{
    ESP_LOGI(TAG, "Input initialization stub");
}

bool input_get_event(input_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    event->key = INPUT_KEY_NONE;
    event->character = '\0';

    return false;
}