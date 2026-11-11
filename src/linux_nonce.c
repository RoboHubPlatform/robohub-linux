/**
 * @file linux_nonce.c
 * @brief The platform's nonce source: the kernel's random bytes.
 *
 * A Linux host has getrandom(2), which blocks only until the kernel's pool is
 * seeded at boot and never repeats. See robohub_platform_nonce in platform.h.
 *
 * SPDX-License-Identifier: MIT
 */

#define _DEFAULT_SOURCE
#include "platform.h"

#include <sys/random.h>

int robohub_platform_nonce(rh_u8* out, rh_size length)
{
    rh_size done = 0;
    while (done < length) {
        ssize_t got = getrandom(out + done, length - done, 0);
        if (got <= 0) {
            return -1;
        }
        done += (rh_size)got;
    }
    return 0;
}
