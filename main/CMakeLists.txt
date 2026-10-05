idf_component_register(
    SRCS
        "main.c"
        "keyboard.c"
        "display.c"
        "editor.c"
        "storage.c"
        "system.c"
        "input/input.c"
    INCLUDE_DIRS "." "input"
    REQUIRES esp_lcd driver lvgl nvs_flash bt
    PRIV_REQUIRES esp_hid
)