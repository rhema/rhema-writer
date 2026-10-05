#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

typedef enum {
    INPUT_KEY_NONE = 0,

    INPUT_KEY_CHAR,

    INPUT_KEY_ENTER,
    INPUT_KEY_BACKSPACE,
    INPUT_KEY_DELETE,
    INPUT_KEY_TAB,
    INPUT_KEY_ESCAPE,

    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,

    INPUT_KEY_HOME,
    INPUT_KEY_END,

    INPUT_KEY_CTRL_S,
    INPUT_KEY_CTRL_O,
    INPUT_KEY_CTRL_I,
} input_key_t;

typedef struct {
    input_key_t key;
    char character;
} input_event_t;

void input_init(void);
bool input_get_event(input_event_t *event);

#endif