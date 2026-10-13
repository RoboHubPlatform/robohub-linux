/**
 * @file linux.h
 * @brief What the Linux platform reads from its environment.
 *
 * A host has no board file to read its devices from, so the few a platform needs
 * are named in the environment. Everything else the contracts supply has a
 * default — the console is standard output, the clock is CLOCK_MONOTONIC — and a
 * deployment that names nothing still runs, with nothing but sockets.
 *
 * @project RoboHub Platform
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef INCLUDE_ROBOHUB_PLATFORM_LINUX_H
#define INCLUDE_ROBOHUB_PLATFORM_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The UARTs, by the device node they are.
 *
 *   RHO_UART0  the tty registered as "uart0" — the upstream link, in a relay
 *              whose upstream is a serial port rather than the console.
 *   RHO_UART1  the tty registered as "uart1" — the downstream link a board is
 *              wired to, the port a Pico 2W relay calls `uart1`.
 *
 * A variable that is unset registers no UART of that name; a program that opens
 * one is told it is not found rather than reaching a port that is not there.
 */

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_ROBOHUB_PLATFORM_LINUX_H */
