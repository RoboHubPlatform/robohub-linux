/**
 * @file linux_update.c
 * @brief robohub-common's update slot on a Linux host: two image files and a
 *        state file (LNX-0009, robohub ADR-0022).
 *
 * Where: `RH_UPDATE_DIR`, else `$RH_STATE_DIR/update`; with neither, the host is
 * not updatable. In it:
 *
 *   image-0, image-1   the images an update wrote (executables)
 *   image-N.part       the one being written
 *   state              running=, pending=, tried=, id=, fw=, sec=, security=,
 *                      outcome=, oid=, ofw=, osec=
 *
 * Slot 0 is the program as it was installed until an update writes image-0;
 * updates go to the slot that does not run.
 *
 * How an image is booted once: rh_update_boot_once() records it as pending and
 * ends the program (exit code 75), so its supervisor — systemd's Restart=always
 * under the keeper — starts it again. At that start, robohub_platform_init sees
 * the pending image, marks it tried and execs it with RH_UPDATE_TRIAL naming the
 * update, so the image knows it is on trial (rh_update_pending). It confirms, or
 * it does not: any later start that still finds it pending and tried, without
 * that mark, goes back to the slot that was confirmed and keeps the outcome —
 * rolled back — for the managed layer to report.
 *
 * Every other start runs the confirmed slot, exec'ing it when the program
 * started is not it.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include "robohub/update_slot.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Named on the link (--undefined) so this object, and the strong functions
 * below, are always taken from the platform's archive. */
// @req LNX-0009
const char rh_update_platform[] = "linux";

#define LINUX_UPDATE_DEFAULT_SLOT (64u * 1024u * 1024u)
#define LINUX_UPDATE_EXIT_RESTART 75

typedef struct {
    int running;            /* the confirmed slot: 0 or 1 */
    int pending;            /* the slot booted once and not confirmed, or -1 */
    int tried;              /* the pending slot was started */
    rh_update_info info;    /* kept with the pending slot */
    uint32_t security;      /* the board's security version */
    int outcome;            /* rh_update_outcome, until taken */
    rh_update_info outcome_info;
} linux_update_state;

static linux_update_state state;
static int state_loaded;
static int trial;           /* this process is the pending image's trial */

static int write_slot = -1; /* the slot begun, or -1 */
static uint32_t write_size;
static uint32_t written;

static int update_dir(char *out, size_t size)
{
    const char *dir = getenv("RH_UPDATE_DIR");
    if (dir != 0 && dir[0] != '\0') {
        return snprintf(out, size, "%s", dir) < (int)size ? 0 : -1;
    }
    dir = getenv("RH_STATE_DIR");
    if (dir != 0 && dir[0] != '\0') {
        return snprintf(out, size, "%s/update", dir) < (int)size ? 0 : -1;
    }
    return -1;
}

static int update_path(char *out, size_t size, const char *name)
{
    char dir[PATH_MAX];
    if (update_dir(dir, sizeof dir) != 0) {
        return -1;
    }
    (void)mkdir(dir, 0700);
    return snprintf(out, size, "%s/%s", dir, name) < (int)size ? 0 : -1;
}

static int image_path(char *out, size_t size, int slot, int part)
{
    char name[32];
    snprintf(name, sizeof name, part ? "image-%d.part" : "image-%d", slot);
    return update_path(out, size, name);
}

static void state_defaults(void)
{
    memset(&state, 0, sizeof state);
    state.running = 0;
    state.pending = -1;
}

static void state_load(void)
{
    if (state_loaded) {
        return;
    }
    state_loaded = 1;
    state_defaults();
    char path[PATH_MAX];
    if (update_path(path, sizeof path, "state") != 0) {
        return;
    }
    FILE *f = fopen(path, "r");
    if (f == 0) {
        return;
    }
    char key[32];
    unsigned long value;
    long svalue;
    char line[96];
    while (fgets(line, sizeof line, f) != 0) {
        if (sscanf(line, "%31[a-z]=%ld", key, &svalue) != 2) {
            continue;
        }
        value = (unsigned long)svalue;
        if (strcmp(key, "running") == 0) state.running = svalue == 1 ? 1 : 0;
        else if (strcmp(key, "pending") == 0) state.pending = (svalue == 0 || svalue == 1) ? (int)svalue : -1;
        else if (strcmp(key, "tried") == 0) state.tried = svalue != 0;
        else if (strcmp(key, "id") == 0) state.info.update_id = (uint32_t)value;
        else if (strcmp(key, "fw") == 0) state.info.firmware_version = (uint16_t)value;
        else if (strcmp(key, "sec") == 0) state.info.security_version = (uint32_t)value;
        else if (strcmp(key, "security") == 0) state.security = (uint32_t)value;
        else if (strcmp(key, "outcome") == 0) state.outcome = (int)svalue;
        else if (strcmp(key, "oid") == 0) state.outcome_info.update_id = (uint32_t)value;
        else if (strcmp(key, "ofw") == 0) state.outcome_info.firmware_version = (uint16_t)value;
        else if (strcmp(key, "osec") == 0) state.outcome_info.security_version = (uint32_t)value;
    }
    fclose(f);
}

/* Written to a temporary file and renamed over the old, so a power cut leaves
 * one whole state or the other. */
static int state_save(void)
{
    char path[PATH_MAX], tmp[PATH_MAX];
    if (update_path(path, sizeof path, "state") != 0 || update_path(tmp, sizeof tmp, "state.tmp") != 0) {
        return -1;
    }
    FILE *f = fopen(tmp, "w");
    if (f == 0) {
        return -1;
    }
    fprintf(f, "running=%d\npending=%d\ntried=%d\nid=%lu\nfw=%u\nsec=%lu\nsecurity=%lu\n"
               "outcome=%d\noid=%lu\nofw=%u\nosec=%lu\n",
            state.running, state.pending, state.tried, (unsigned long)state.info.update_id,
            (unsigned)state.info.firmware_version, (unsigned long)state.info.security_version,
            (unsigned long)state.security, state.outcome, (unsigned long)state.outcome_info.update_id,
            (unsigned)state.outcome_info.firmware_version, (unsigned long)state.outcome_info.security_version);
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return rename(tmp, path) == 0 ? 0 : -1;
}

/* Run `slot`'s image if this program is not it; marked as the trial when it is
 * the pending one. Returns when there is nothing to exec (slot 0 as installed,
 * or this program already is it). */
static void exec_slot(int slot, int as_trial)
{
    char image[PATH_MAX], self[PATH_MAX], real_image[PATH_MAX];
    if (image_path(image, sizeof image, slot, 0) != 0 || access(image, X_OK) != 0) {
        return;
    }
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = '\0';
        if (realpath(image, real_image) != 0 && strcmp(self, real_image) == 0) {
            return;
        }
    }
    if (as_trial) {
        char mark[16];
        snprintf(mark, sizeof mark, "%lu", (unsigned long)state.info.update_id);
        setenv("RH_UPDATE_TRIAL", mark, 1);
    } else {
        unsetenv("RH_UPDATE_TRIAL");
    }
    char cmdline[PATH_MAX];
    /* The program's own arguments, as it was started. */
    FILE *f = fopen("/proc/self/cmdline", "r");
    char *argv[64];
    int argc = 0;
    size_t len = f != 0 ? fread(cmdline, 1, sizeof cmdline - 1, f) : 0;
    if (f != 0) {
        fclose(f);
    }
    cmdline[len] = '\0';
    for (size_t i = 0; i < len && argc < 63; i += strlen(&cmdline[i]) + 1) {
        argv[argc++] = &cmdline[i];
    }
    argv[0] = image;
    argv[argc] = 0;
    fflush(0);
    execv(image, argv);
    /* An image that cannot be run is not run: carry on as this program. */
}

/* At every start, from robohub_board_register: choose the slot that runs. */
// @req LNX-0009
void linux_update_start(void)
{
    char dir[PATH_MAX];
    if (update_dir(dir, sizeof dir) != 0) {
        return;
    }
    state_loaded = 0;
    state_load();
    const char *mark = getenv("RH_UPDATE_TRIAL");
    if (state.pending >= 0) {
        if (!state.tried) {
            state.tried = 1;
            if (state_save() == 0) {
                exec_slot(state.pending, 1);
            }
            trial = 1; /* the pending image is this program: nothing to exec */
            return;
        }
        if (mark != 0 && (uint32_t)strtoul(mark, 0, 10) == state.info.update_id) {
            trial = 1;
            unsetenv("RH_UPDATE_TRIAL");
            return;
        }
        /* Started again without having confirmed: back to what was kept. */
        state.outcome = RH_UPDATE_ROLLED_BACK;
        state.outcome_info = state.info;
        state.pending = -1;
        state.tried = 0;
        (void)state_save();
    }
    unsetenv("RH_UPDATE_TRIAL");
    exec_slot(state.running, 0);
}

// @req LNX-0009
void rh_update_caps_get(rh_update_caps *caps)
{
    if (caps == 0) {
        return;
    }
    char dir[PATH_MAX];
    caps->updatable = update_dir(dir, sizeof dir) == 0;
    caps->slot_size = 0u;
    if (caps->updatable) {
        const char *size = getenv("RH_UPDATE_SLOT_SIZE");
        caps->slot_size = size != 0 && size[0] != '\0' ? (uint32_t)strtoul(size, 0, 10) : LINUX_UPDATE_DEFAULT_SLOT;
    }
}

static int inactive_slot(void)
{
    state_load();
    return state.running == 0 ? 1 : 0;
}

// @req LNX-0009
RoboHubError rh_update_begin(uint32_t size)
{
    rh_update_caps caps;
    rh_update_caps_get(&caps);
    if (!caps.updatable) {
        return ROBOHUB_ERR_NOT_SUPPORTED;
    }
    if (size == 0u || size > caps.slot_size) {
        return ROBOHUB_ERR_NO_MEM;
    }
    int slot = inactive_slot();
    char path[PATH_MAX];
    if (image_path(path, sizeof path, slot, 1) != 0) {
        return ROBOHUB_ERR_IO;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return ROBOHUB_ERR_IO;
    }
    close(fd);
    write_slot = slot;
    write_size = size;
    written = 0u;
    return ROBOHUB_OK;
}

// @req LNX-0009
RoboHubError rh_update_write(uint32_t offset, const uint8_t *data, uint32_t length)
{
    if (write_slot < 0 || offset != written) {
        return ROBOHUB_ERR_STATE;
    }
    if (data == 0 || length > write_size - written) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    char path[PATH_MAX];
    if (image_path(path, sizeof path, write_slot, 1) != 0) {
        return ROBOHUB_ERR_IO;
    }
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return ROBOHUB_ERR_IO;
    }
    ssize_t n = pwrite(fd, data, length, (off_t)offset);
    close(fd);
    if (n != (ssize_t)length) {
        return ROBOHUB_ERR_IO;
    }
    written += length;
    return ROBOHUB_OK;
}

// @req LNX-0009
RoboHubError rh_update_read(uint32_t offset, uint8_t *out, uint32_t length)
{
    if (write_slot < 0) {
        return ROBOHUB_ERR_STATE;
    }
    if (out == 0 || offset > written || length > written - offset) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    char path[PATH_MAX];
    if (image_path(path, sizeof path, write_slot, 1) != 0) {
        return ROBOHUB_ERR_IO;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return ROBOHUB_ERR_IO;
    }
    ssize_t n = pread(fd, out, length, (off_t)offset);
    close(fd);
    return n == (ssize_t)length ? ROBOHUB_OK : ROBOHUB_ERR_IO;
}

// @req LNX-0009
RoboHubError rh_update_boot_once(const rh_update_info *info)
{
    if (write_slot < 0 || written != write_size || info == 0) {
        return ROBOHUB_ERR_STATE;
    }
    char part[PATH_MAX], image[PATH_MAX];
    if (image_path(part, sizeof part, write_slot, 1) != 0 || image_path(image, sizeof image, write_slot, 0) != 0) {
        return ROBOHUB_ERR_IO;
    }
    if (chmod(part, 0700) != 0 || rename(part, image) != 0) {
        return ROBOHUB_ERR_IO;
    }
    state_load();
    state.pending = write_slot;
    state.tried = 0;
    state.info = *info;
    if (state_save() != 0) {
        return ROBOHUB_ERR_IO;
    }
    write_slot = -1;
    fflush(0);
    exit(LINUX_UPDATE_EXIT_RESTART);
}

// @req LNX-0009
bool rh_update_pending(rh_update_info *out)
{
    state_load();
    if (!trial || state.pending < 0) {
        return false;
    }
    if (out != 0) {
        *out = state.info;
    }
    return true;
}

// @req LNX-0009
RoboHubError rh_update_confirm(void)
{
    if (!rh_update_pending(0)) {
        return ROBOHUB_ERR_STATE;
    }
    state.running = state.pending;
    if (state.info.security_version > state.security) {
        state.security = state.info.security_version;
    }
    state.outcome = RH_UPDATE_KEPT;
    state.outcome_info = state.info;
    state.pending = -1;
    state.tried = 0;
    trial = 0;
    return state_save() == 0 ? ROBOHUB_OK : ROBOHUB_ERR_IO;
}

/* Given up: the outcome is rolled back, the pending image forgotten, and the
 * program ends so its supervisor starts the confirmed slot again. */
// @req LNX-0009
RoboHubError rh_update_reject(void)
{
    if (!rh_update_pending(0)) {
        return ROBOHUB_ERR_STATE;
    }
    state.outcome = RH_UPDATE_ROLLED_BACK;
    state.outcome_info = state.info;
    state.pending = -1;
    state.tried = 0;
    trial = 0;
    if (state_save() != 0) {
        return ROBOHUB_ERR_IO;
    }
    fflush(0);
    exit(LINUX_UPDATE_EXIT_RESTART);
}

// @req LNX-0009
rh_update_outcome rh_update_take_outcome(rh_update_info *out)
{
    state_load();
    rh_update_outcome outcome = (rh_update_outcome)state.outcome;
    if (outcome == RH_UPDATE_NONE) {
        return RH_UPDATE_NONE;
    }
    if (out != 0) {
        *out = state.outcome_info;
    }
    state.outcome = RH_UPDATE_NONE;
    (void)state_save();
    return outcome;
}

// @req LNX-0009
uint32_t rh_update_security_version(void)
{
    state_load();
    return state.security;
}

// @req LNX-0009
int rh_update_running_slot(void)
{
    char dir[PATH_MAX];
    if (update_dir(dir, sizeof dir) != 0) {
        return -1;
    }
    state_load();
    return (trial && state.pending >= 0) ? state.pending : state.running;
}
