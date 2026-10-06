#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "input.h"
#include "display.h"

static const char *TAG = "EDITOR";

#define SCREEN_ROWS 14
#define SCREEN_COLS 28

enum KEY_ACTION { ARROW_LEFT = 1000, ARROW_RIGHT, ARROW_UP, ARROW_DOWN };

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx, cy;
    int rowoff, coloff;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    int info_mode;
};

static struct editorConfig E;

void initEditor(void) {
    E.cx = 0; E.cy = 0;
    E.rowoff = 0; E.coloff = 0;
    E.numrows = 0; E.row = NULL;
    E.dirty = 0;
    E.filename = strdup("untitled.txt");
    E.info_mode = 0;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void editorInsertRow(int at, char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    if (at != E.numrows) {
        memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    E.dirty++;
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    free(E.row[at].chars);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorDelChar(void) {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        memmove(&row->chars[E.cx - 1], &row->chars[E.cx], row->size - E.cx + 1);
        row->size--;
        E.cx--;
        E.dirty++;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

void editorDelCharForward(void) {
    if (E.cy == E.numrows) return;
    
    erow *row = &E.row[E.cy];
    if (E.cx < row->size) {
        memmove(&row->chars[E.cx], &row->chars[E.cx + 1], row->size - E.cx);
        row->size--;
        E.dirty++;
    } else if (E.cy < E.numrows - 1) {
        editorRowAppendString(row, E.row[E.cy + 1].chars, E.row[E.cy + 1].size);
        editorDelRow(E.cy + 1);
    }
}

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline(void) {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[E.cx] = '\0';
    }
    E.cy++;
    E.cx = 0;
}

void editorScroll(void) {
    int cursor_v_y = 0;
    
    for (int i = 0; i < E.cy; i++) {
        cursor_v_y += (E.row[i].size / SCREEN_COLS) + 1;
    }
    cursor_v_y += (E.cx / SCREEN_COLS);

    int offset_v_y = 0;
    for (int i = 0; i < E.rowoff; i++) {
        offset_v_y += (E.row[i].size / SCREEN_COLS) + 1;
    }

    if (cursor_v_y < offset_v_y) {
        E.rowoff = E.cy;
    }
    
    while (cursor_v_y >= offset_v_y + SCREEN_ROWS) {
        offset_v_y += (E.row[E.rowoff].size / SCREEN_COLS) + 1;
        E.rowoff++;
    }
}

void editorCalcStats(int *char_count, int *word_count) {
    *char_count = 0;
    *word_count = 0;
    for (int i = 0; i < E.numrows; i++) {
        *char_count += E.row[i].size;
        bool in_word = false;
        for (int j = 0; j < E.row[i].size; j++) {
            if (isspace((unsigned char)E.row[i].chars[j])) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                (*word_count)++;
            }
        }
    }
}

void getBatteryStats(float *voltage, int *percentage) {
    *voltage = 4.05f; 
    *percentage = 92;
}

void editorRefreshInfoScreen(void) {
    // Declared static to prevent stack frame exhaustion
    static char display_buf[1024];
    int offset = 0;

    int chars = 0, words = 0;
    editorCalcStats(&chars, &words);

    float voltage = 0.0f;
    int battery_pct = 0;
    getBatteryStats(&voltage, &battery_pct);

    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "=== RHEMA WRITER INFO ===\n\n");
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, " File: %s\n", E.filename);
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, " Modified: %s\n\n", E.dirty ? "Yes" : "No");

    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, " Document Stats:\n");
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "  Words:      %d\n", words);
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "  Characters: %d\n", chars);
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "  Lines:      %d\n\n", E.numrows);

    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, " Hardware Telemetry:\n");
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "  Battery:    %d%%\n", battery_pct);
    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "  Voltage:    %.2fV\n\n", voltage);

    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, " Press CTRL-I to return.");

    display_set_text(display_buf);
}

void editorRefreshScreen(void) {
    if (E.info_mode) {
        editorRefreshInfoScreen();
        return;
    }

    editorScroll();

    // Declared static to prevent stack frame exhaustion
    static char display_buf[2048];
    int offset = 0;

    int screen_y = 0;
    int logical_row = E.rowoff; 
    int char_idx = 0; 

    while (screen_y < SCREEN_ROWS) {
        if (logical_row >= E.numrows) {
            if (logical_row == E.cy && char_idx == E.cx && E.numrows == 0) {
                offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "#00aaff _#\n");
            } else {
                offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "~\n");
            }
            screen_y++;
            continue;
        }

        erow *r = &E.row[logical_row];
        
        for (int j = 0; j < SCREEN_COLS; j++) {
            bool is_cursor = (logical_row == E.cy && char_idx == E.cx);
            
            char c = ' ';
            bool eol = false;
            
            if (char_idx < r->size) {
                c = r->chars[char_idx];
            } else if (char_idx == r->size) {
                c = ' ';
                eol = true;
            } else {
                eol = true;
            }

            if (is_cursor) {
                if (c == ' ') c = '_'; 
                offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "#00aaff ");
                if (c == '#') { display_buf[offset++] = '#'; display_buf[offset++] = '#'; }
                else { display_buf[offset++] = c; }
                display_buf[offset++] = '#';
            } else {
                if (char_idx < r->size) {
                    if (c == '#') { display_buf[offset++] = '#'; display_buf[offset++] = '#'; }
                    else { display_buf[offset++] = c; }
                } else {
                    display_buf[offset++] = ' ';
                }
            }

            char_idx++;
            if (eol && char_idx > r->size) break; 
        }
        display_buf[offset++] = '\n';
        screen_y++;

        if (char_idx > r->size) {
            logical_row++;
            char_idx = 0;
        }
    }

    if (time(NULL) - E.statusmsg_time < 3 && strlen(E.statusmsg) > 0) {
        offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, ">> %s", E.statusmsg);
    } else {
        offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "---- RHEMA WRITER %s----", E.dirty ? "*" : "");
    }
    
    display_set_text(display_buf);
}

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch(key) {
    case ARROW_LEFT:
        if (E.cx > 0) E.cx--;
        else if (E.cy > 0) { E.cy--; E.cx = E.row[E.cy].size; }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size) E.cx++;
        else if (row && E.cx == row->size) { E.cy++; E.cx = 0; }
        break;
    case ARROW_UP:
        if (E.cy > 0) E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows) E.cy++;
        break;
    }
    
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) E.cx = rowlen;
}

void editorProcessKeypress(void) {
    input_event_t event;
    if (!input_get_event(&event)) return;

    if (E.info_mode) {
        if (event.key == INPUT_KEY_CTRL_I || event.key == INPUT_KEY_ESCAPE || event.key == INPUT_KEY_ENTER) {
            E.info_mode = 0;
            editorRefreshScreen();
        }
        return;
    }

    switch (event.key) {
        case INPUT_KEY_CHAR: editorInsertChar(event.character); break;
        case INPUT_KEY_ENTER: editorInsertNewline(); break;
        case INPUT_KEY_BACKSPACE: editorDelChar(); break;
        case INPUT_KEY_DELETE: editorDelCharForward(); break;
        case INPUT_KEY_LEFT: editorMoveCursor(ARROW_LEFT); break;
        case INPUT_KEY_RIGHT: editorMoveCursor(ARROW_RIGHT); break;
        case INPUT_KEY_UP: editorMoveCursor(ARROW_UP); break;
        case INPUT_KEY_DOWN: editorMoveCursor(ARROW_DOWN); break;
        case INPUT_KEY_CTRL_I: E.info_mode = 1; break;
        case INPUT_KEY_CTRL_S: editorSetStatusMessage("File Saved (Stub)"); break;
        case INPUT_KEY_CTRL_O: editorSetStatusMessage("File Opened (Stub)"); break;
        default: break;
    }
    editorRefreshScreen();
}

void editor_task(void *pvParameters) {
    initEditor();
    editorRefreshScreen();

    while (1) {
        editorProcessKeypress();
    }
}

void editor_init(void) {
    ESP_LOGI(TAG, "Starting Editor Task...");
    // Increased stack size from 4096 to 6144 to safely accommodate rendering strings
    xTaskCreate(editor_task, "editor_task", 6144, NULL, 3, NULL);
}