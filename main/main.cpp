#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "lvgl_port_bsp.h"
#include "user_app.h"
#include "user_config.h"
#include <stdio.h>

#include "lv_demos.h"
#include "lvgl.h"

static const char *TAG = "esp32_1_32";

extern "C" void app_main(void) {
    // On battery the PWR button only closes the power latch while a finger is
    // on it. The board keeps itself alive after the button is released, so
    // this goes first, before anything slower can drop the rail.
    gpio_set_direction((gpio_num_t) SYS_POWER_IO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t) SYS_POWER_IO_PIN, 1);

    user_app_init();

    Lvgl_PortInit();

    if (lvgl_lock(0)) {
        user_ui_init();
        lvgl_unlock();
    }
}
