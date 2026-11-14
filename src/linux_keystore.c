/**
 * @file linux_keystore.c
 * @brief The device keystore on a Linux host: one 0600 file, replaced atomically.
 *
 * The file holds the two 40-byte A/B records of robohub/keystore_core.h, one
 * after the other. A write never changes the file in place: it writes the whole
 * new contents to `<file>.tmp` (created 0600, fsync'd), renames it over the file
 * and fsyncs the directory, so a crash leaves either the old file or the new one.
 * The A/B logic is kept anyway, so a host and a board store keys the same way.
 *
 * Where the file is, first that is set:
 *
 *   RH_KEYSTORE_FILE        the file itself
 *   RH_STATE_DIR            <dir>/keystore.bin (the node's state directory)
 *   RH_SECURE_KEY_FILE      <key file>.keystore, beside the key it supersedes
 *
 * With none of them set there is no store: load finds nothing and store refuses,
 * the same answers the weak default gives.
 *
 * SPDX-License-Identifier: MIT
 */

#define _DEFAULT_SOURCE
#include "robohub/keystore.h"
#include "robohub/keystore_core.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Named on the link (--undefined) so this object, and the strong functions
 * below, are always taken from the platform's archive; see keystore_core.h. */
const char rh_keystore_platform[] = "linux";

#define LINUX_KEYSTORE_BYTES (RH_KEYSTORE_SLOTS * RH_KEYSTORE_RECORD_SIZE)

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* The keystore file's path, or -1 when nothing names one. */
static int keystore_path(char *out, size_t size)
{
    const char *file = getenv("RH_KEYSTORE_FILE");
    if (file != 0 && file[0] != '\0') {
        return snprintf(out, size, "%s", file) < (int)size ? 0 : -1;
    }
    const char *dir = getenv("RH_STATE_DIR");
    if (dir != 0 && dir[0] != '\0') {
        return snprintf(out, size, "%s/keystore.bin", dir) < (int)size ? 0 : -1;
    }
    const char *key = getenv("RH_SECURE_KEY_FILE");
    if (key != 0 && key[0] != '\0') {
        return snprintf(out, size, "%s.keystore", key) < (int)size ? 0 : -1;
    }
    return -1;
}

/* The whole file into `buf`; a missing or short file reads as erased. */
static int read_all(const char *path, uint8_t *buf)
{
    memset(buf, 0xFF, LINUX_KEYSTORE_BYTES);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    size_t done = 0;
    while (done < LINUX_KEYSTORE_BYTES) {
        ssize_t got = read(fd, buf + done, LINUX_KEYSTORE_BYTES - done);
        if (got < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (got == 0) break;
        done += (size_t)got;
    }
    close(fd);
    return 0;
}

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t put = write(fd, buf + done, len - done);
        if (put < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)put;
    }
    return 0;
}

static void sync_parent(const char *path)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash == 0) {
        snprintf(dir, sizeof(dir), ".");
    } else if (slash == dir) {
        dir[1] = '\0';
    } else {
        *slash = '\0';
    }
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
}

static int linux_read(void *ctx, unsigned slot, uint8_t *buf, size_t len)
{
    const char *path = (const char *)ctx;
    uint8_t all[LINUX_KEYSTORE_BYTES];
    if (slot >= RH_KEYSTORE_SLOTS || len > RH_KEYSTORE_RECORD_SIZE || read_all(path, all) != 0) {
        return -1;
    }
    memcpy(buf, all + slot * RH_KEYSTORE_RECORD_SIZE, len);
    memset(all, 0, sizeof(all));
    return 0;
}

static int linux_write(void *ctx, unsigned slot, const uint8_t *buf, size_t len)
{
    const char *path = (const char *)ctx;
    uint8_t all[LINUX_KEYSTORE_BYTES];
    char tmp[PATH_MAX];
    int result = -1;

    if (slot >= RH_KEYSTORE_SLOTS || len > RH_KEYSTORE_RECORD_SIZE) {
        return -1;
    }
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    if (read_all(path, all) != 0) {
        return -1;
    }
    memcpy(all + slot * RH_KEYSTORE_RECORD_SIZE, buf, len);

    (void)unlink(tmp); /* a leftover from a crash */
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        goto done;
    }
    /* The mode is set explicitly too, whatever the umask said. */
    if (fchmod(fd, 0600) != 0 || write_all(fd, all, sizeof(all)) != 0 || fsync(fd) != 0) {
        close(fd);
        (void)unlink(tmp);
        goto done;
    }
    if (close(fd) != 0 || rename(tmp, path) != 0) {
        (void)unlink(tmp);
        goto done;
    }
    sync_parent(path);
    result = 0;

done:
    memset(all, 0, sizeof(all));
    return result;
}

int rh_keystore_load(rh_key_t *out)
{
    char path[PATH_MAX];
    if (keystore_path(path, sizeof(path)) != 0) {
        return -1;
    }
    const rh_keystore_backend_t backend = { path, linux_read, 0, linux_write };
    return rh_keystore_core_load(&backend, out);
}

int rh_keystore_store(const rh_key_t *key)
{
    char path[PATH_MAX];
    if (keystore_path(path, sizeof(path)) != 0) {
        return -1;
    }
    const rh_keystore_backend_t backend = { path, linux_read, 0, linux_write };
    return rh_keystore_core_store(&backend, key);
}
