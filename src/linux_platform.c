/**
 * @file linux_platform.c
 * @brief The RoboHub platform for a Linux host: console, clock, serial, registry.
 *
 * A board's platform brings up clocks and pins; a host's already has them. What
 * this supplies is the same contracts over the machine instead of a chip: the
 * console is a file descriptor, the clock is CLOCK_MONOTONIC, a serial port is a
 * tty, and the buses a host has no answer for — OneWire, I2C, SPI, GPIO — are
 * simply not registered, which its smoke reports as absent rather than failed.
 *
 * That is what makes a **gateway-only mode** run on the machine it aggregates
 * from: the runtime, the transport and the telemetry bridge are the board's, and
 * only the platform beneath them changes.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "platform.h"
#include "registry.h"
#include "types.h"
#include "error.h"
#include "robohub/hal/console.h"
#include "robohub/hal/timer.h"
#include "robohub/hal/uart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */
/*  Console: the standard descriptors                                         */
/* -------------------------------------------------------------------------- */

static RoboHubConsole linux_console;

RoboHubConsoleError robohub_console_init(void)
{
    linux_console.ready = 1u;
    linux_console.written = 0u;
    return ROBOHUB_CONSOLE_OK;
}

RoboHubConsoleError robohub_console_write(const char* data, size_t len)
{
    return robohub_console_write_raw((const uint8_t*)data, len);
}

RoboHubConsoleError robohub_console_puts(const char* text)
{
    if (text == 0) {
        return ROBOHUB_CONSOLE_ERR_INVALID;
    }
    return robohub_console_write_raw((const uint8_t*)text, strlen(text));
}

RoboHubConsoleError robohub_console_write_raw(const uint8_t* data, size_t len)
{
    if (data == 0 || len == 0u) {
        return ROBOHUB_CONSOLE_ERR_INVALID;
    }
    size_t at = 0;
    while (at < len) {
        ssize_t written = write(STDOUT_FILENO, data + at, len - at);
        if (written <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return ROBOHUB_CONSOLE_ERR_TIMEOUT;
        }
        at += (size_t)written;
    }
    linux_console.written += len;
    return ROBOHUB_CONSOLE_OK;
}

int robohub_console_read_byte(void)
{
    uint8_t byte = 0;
    ssize_t got = read(STDIN_FILENO, &byte, 1);
    if (got == 1) {
        return (int)byte;
    }
    return -1;
}

/* -------------------------------------------------------------------------- */
/*  Clock: CLOCK_MONOTONIC                                                    */
/* -------------------------------------------------------------------------- */

static void cpu_delay_ms(RoboHubTimer* self, rh_u32 ms)
{
    (void)self;
    struct timespec request = { .tv_sec = (time_t)(ms / 1000u),
                                .tv_nsec = (long)((ms % 1000u) * 1000000u) };
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {
    }
}

static void cpu_delay_us(RoboHubTimer* self, rh_u32 us)
{
    (void)self;
    struct timespec request = { .tv_sec = 0, .tv_nsec = (long)(us * 1000u) };
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {
    }
}

static rh_millis cpu_millis(RoboHubTimer* self)
{
    (void)self;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (rh_millis)((uint64_t)now.tv_sec * 1000u + (uint64_t)(now.tv_nsec / 1000000));
}

static rh_micros cpu_micros(RoboHubTimer* self)
{
    (void)self;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (rh_micros)((uint64_t)now.tv_sec * 1000000u + (uint64_t)(now.tv_nsec / 1000));
}

static RoboHubTimer linux_timer = {
    .name = "system_timer",
    .context = 0,
    .get_millis = cpu_millis,
    .get_micros = cpu_micros,
    .delay_ms = cpu_delay_ms,
    .delay_us = cpu_delay_us,
};

/* -------------------------------------------------------------------------- */
/*  Serial: a tty                                                             */
/* -------------------------------------------------------------------------- */

/** A tty opened raw at a baud a board would use. */
typedef struct {
    int fd;
    /** The device node, from the environment. The UART's own name is what a
     *  program opens it by; this is what the machine calls it. */
    const char* path;
} LinuxSerial;

static speed_t baud_constant(rh_u32 baud)
{
    switch (baud) {
    case 9600u: return B9600;
    case 19200u: return B19200;
    case 38400u: return B38400;
    case 57600u: return B57600;
    case 230400u: return B230400;
    case 115200u:
    default: return B115200;
    }
}

static RoboHubError serial_init(RoboHubUART* self, rh_u32 baudrate)
{
    if (self == 0 || self->context == 0) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    LinuxSerial* serial = (LinuxSerial*)self->context;
    if (serial->fd >= 0) {
        return ROBOHUB_OK;
    }
    const char* path = serial->path;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return ROBOHUB_ERR_NOT_FOUND;
    }
    struct termios settings;
    if (tcgetattr(fd, &settings) != 0) {
        close(fd);
        return ROBOHUB_ERR_IO;
    }
    cfmakeraw(&settings);
    speed_t speed = baud_constant(baudrate);
    cfsetispeed(&settings, speed);
    cfsetospeed(&settings, speed);
    if (tcsetattr(fd, TCSANOW, &settings) != 0) {
        close(fd);
        return ROBOHUB_ERR_IO;
    }
    serial->fd = fd;
    return ROBOHUB_OK;
}

static RoboHubError serial_write(RoboHubUART* self, const rh_u8* data, rh_size len)
{
    if (self == 0 || self->context == 0 || data == 0) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    LinuxSerial* serial = (LinuxSerial*)self->context;
    if (serial->fd < 0) {
        return ROBOHUB_ERR_STATE;
    }
    size_t at = 0;
    while (at < len) {
        ssize_t written = write(serial->fd, data + at, len - at);
        if (written <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return ROBOHUB_ERR_IO;
        }
        at += (size_t)written;
    }
    return ROBOHUB_OK;
}

/** Read what a tty has waiting; a host has no interrupt, so this is the poll. */
static RoboHubError serial_read(RoboHubUART* self, rh_u8* data, rh_size len)
{
    if (self == 0 || self->context == 0 || data == 0) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    LinuxSerial* serial = (LinuxSerial*)self->context;
    if (serial->fd < 0) {
        return ROBOHUB_ERR_STATE;
    }
    ssize_t got = read(serial->fd, data, len);
    if (got > 0) {
        return ROBOHUB_OK;
    }
    return ROBOHUB_ERR_NOT_FOUND;
}

static LinuxSerial serial0 = { .fd = -1 };
static LinuxSerial serial1 = { .fd = -1 };

static RoboHubUART linux_uart0 = {
    .name = "uart0",
    .context = &serial0,
    .init = serial_init,
    .write = serial_write,
    .read = serial_read,
};

static RoboHubUART linux_uart1 = {
    .name = "uart1",
    .context = &serial1,
    .init = serial_init,
    .write = serial_write,
    .read = serial_read,
};

/* -------------------------------------------------------------------------- */
/*  Platform                                                                  */
/* -------------------------------------------------------------------------- */

/** Point a UART at a device named by the environment, and register it under the
 *  name a program opens it by. The variable says which tty; the name is what the
 *  program asks for, and the two are different things: a relay opens "uart1"
 *  whatever the machine calls the device. */
static void register_uart(RoboHubUART* uart, const char* name, const char* variable)
{
    const char* path = getenv(variable);
    if (path == 0 || path[0] == '\0') {
        return;
    }
    LinuxSerial* serial = (LinuxSerial*)uart->context;
    serial->path = path;
    uart->name = (rh_name)name;
    (void)robohub_register_uart((rh_name)name, uart);
}

// A host has no board to bring up: the devices registered here are the board.
//
// `robohub_platform_init` is robohub-common's — it initialises the registry and
// then calls this — so the platform supplies the board hook and nothing else,
// which is how every platform and the common library refer to each other without
// a circular dependency between two archives.
RoboHubError robohub_board_register(void)
{
    (void)robohub_console_init();
    (void)robohub_register_timer("system_timer", &linux_timer);
    register_uart(&linux_uart0, "uart0", "RHO_UART0");
    register_uart(&linux_uart1, "uart1", "RHO_UART1");
    return ROBOHUB_OK;
}
