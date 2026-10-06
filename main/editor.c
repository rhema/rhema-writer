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

#define KILO_VERSION "0.0.1"

#define SCREEN_ROWS 12
#define SCREEN_COLS 24

enum KEY_ACTION {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig {
    int cx, cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
};

static struct editorConfig E;

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);

void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = strdup("document.txt");
    E.screenrows = SCREEN_ROWS;
    E.screencols = SCREEN_COLS;
}

void editorUpdateRow(erow *row) {
    free(row->render);
    row->render = malloc(row->size + 1);
    memcpy(row->render, row->chars, row->size + 1);
    row->rsize = row->size;
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    if (at != E.numrows) {
        memmove(E.row + at + 1, E.row + at, sizeof(E.row[0]) * (E.numrows - at));
        for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len + 1);
    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    E.row[at].idx = at;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(row->chars + at + 1, row->chars + at, row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(row->chars + row->size, s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (row->size <= at) return;
    memmove(row->chars + at, row->chars + at + 1, row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorDelRow(int at) {
    if (at >= E.numrows) return;
    free(E.row[at].render);
    free(E.row[at].chars);
    memmove(E.row + at, E.row + at + 1, sizeof(E.row[0]) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx++;
    E.numrows--;
    E.dirty++;
}

void editorInsertChar(int c) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    if (filerow >= E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    erow *row = &E.row[filerow];
    editorRowInsertChar(row, filecol, c);
    if (E.cx == E.screencols - 1) E.coloff++;
    else E.cx++;
}

void editorInsertNewline(void) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row) {
        editorInsertRow(filerow, "", 0);
    } else {
        if (filecol >= row->size) filecol = row->size;
        if (filecol == 0) {
            editorInsertRow(filerow, "", 0);
        } else {
            editorInsertRow(filerow + 1, row->chars + filecol, row->size - filecol);
            row = &E.row[filerow];
            row->chars[filecol] = '\0';
            row->size = filecol;
            editorUpdateRow(row);
        }
    }
    if (E.cy == E.screenrows - 1) E.rowoff++;
    else E.cy++;
    E.cx = 0;
    E.coloff = 0;
}

void editorDelChar(void) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        filecol = E.row[filerow - 1].size;
        editorRowAppendString(&E.row[filerow - 1], row->chars, row->size);
        editorDelRow(filerow);
        if (E.cy == 0) E.rowoff--;
        else E.cy--;
        E.cx = filecol;
    } else {
        editorRowDelChar(row, filecol - 1);
        if (E.cx == 0 && E.coloff) E.coloff--;
        else E.cx--;
    }
}

void editorMoveCursor(int key) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    switch(key) {
    case ARROW_LEFT:
        if (E.cx == 0) {
            if (E.coloff) E.coloff--;
            else if (filerow > 0) {
                E.cy--;
                E.cx = E.row[filerow - 1].size;
            }
        } else E.cx--;
        break;
    case ARROW_RIGHT:
        if (row && filecol < row->size) {
            if (E.cx == E.screencols - 1) E.coloff++;
            else E.cx++;
        }
        break;
    case ARROW_UP:
        if (E.cy == 0) { if (E.rowoff) E.rowoff--; }
        else E.cy--;
        break;
    case ARROW_DOWN:
        if (filerow < E.numrows) {
            if (E.cy == E.screenrows - 1) E.rowoff++;
            else E.cy++;
        }
        break;
    }
}

void editorRefreshScreen(void) {
    char display_buf[1024] = {0};
    int offset = 0;

    for (int y = 0; y < E.screenrows; y++) {
        int filerow = E.rowoff + y;
        if (filerow >= E.numrows) {
            offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "~\n");
            continue;
        }

        erow *r = &E.row[filerow];
        int len = r->rsize - E.coloff;
        if (len < 0) len = 0;
        if (len > E.screencols) len = E.screencols;

        if (len > 0) {
            offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "%.*s\n", len, r->render + E.coloff);
        } else {
            offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "\n");
        }
    }

    offset += snprintf(display_buf + offset, sizeof(display_buf) - offset, "[%s] %s\n", E.filename, E.dirty ? "*" : "");
    display_set_text(display_buf);
}

void editorProcessKeypress(void) {
    input_event_t event;
    if (!input_get_event(&event)) return;

    switch (event.key) {
        case INPUT_KEY_CHAR:
            editorInsertChar(event.character);
            break;
        case INPUT_KEY_ENTER:
            editorInsertNewline();
            break;
        case INPUT_KEY_BACKSPACE:
            editorDelChar();
            break;
        case INPUT_KEY_LEFT:
            editorMoveCursor(ARROW_LEFT);
            break;
        case INPUT_KEY_RIGHT:
            editorMoveCursor(ARROW_RIGHT);
            break;
        case INPUT_KEY_UP:
            editorMoveCursor(ARROW_UP);
            break;
        case INPUT_KEY_DOWN:
            editorMoveCursor(ARROW_DOWN);
            break;
        case INPUT_KEY_CTRL_S:
            editorSetStatusMessage("File saved!");
            E.dirty = 0;
            break;
        default:
            break;
    }
    editorRefreshScreen();
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
}

void editor_task(void *pvParameters) {
    initEditor();
    //editorInsertRow(0, "Rhema Writer Online", 19);
    editorRefreshScreen();

    while (1) {
        editorProcessKeypress();
    }
}

void editor_init(void) {
    ESP_LOGI(TAG, "Starting Editor Task...");
    xTaskCreate(editor_task, "editor_task", 4096, NULL, 3, NULL);
}