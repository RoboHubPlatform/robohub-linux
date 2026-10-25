/**
 * @file main.c
 * @brief hello_world — a Linux program that says hello.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"
#include "registry.h"
#include "types.h"
#include "robohub/hal/console.h"
#include "robohub/hal/timer.h"

int main(void)
{
    if (robohub_platform_init() != ROBOHUB_OK) {
        return 1;
    }

    RoboHubTimer* timer = robohub_get_timer("system_timer");

    (void)robohub_console_puts("\nHello world, from a Linux board\n");
    (void)robohub_console_puts("says something every second, from here on\n");
    (void)robohub_console_puts("--------------------------------------\n");

    uint32_t seconds = 0;
    for (;;) {
        seconds++;
        (void)robohub_console_puts("[");
        char digits[12];
        uint32_t count = 0;
        uint32_t value = seconds;
        if (value == 0) {
            digits[count++] = '0';
        }
        while (value > 0) {
            digits[count++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (count > 0) {
            (void)robohub_console_write(&digits[--count], 1);
        }
        (void)robohub_console_puts(" s] hello from the board, in C\n");

        if (timer != NULL) {
            timer->delay_ms(timer, 1000u);
        }
    }
}
