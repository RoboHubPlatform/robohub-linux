/*
 * test_linux_update.c — the Linux update slot across real restarts (LNX-0009).
 *
 * The test is its own board: it runs itself as a child for each start, with a
 * step name, and the image it writes is a copy of its own executable — so the
 * exec into a slot is real, and the copy runs the same steps. The parent is the
 * supervisor that starts the board again after it ends.
 *
 *   write       an image is written, read back and booted once (exit 75)
 *   trial       the next start runs it, on trial, and ends without confirming
 *   rollback    the start after that goes back, and says so once
 *   write       again
 *   confirm     its trial confirms it
 *   kept        later starts run it, its security version is the board's
 *   again       and the outcome was taken once
 *
 * @project RoboHub Platform
 * @license MIT
 */

#define _GNU_SOURCE
#include "platform.h"
#include "robohub/update_slot.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char self_path[4096];

static int step(const char *name)
{
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        execl(self_path, self_path, name, (char *)0);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static int running_copy(void)
{
    /* The image, not the program as installed. */
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = '\0';
    return strstr(exe, "/image-") != 0;
}

static int child(const char *name)
{
    assert(robohub_platform_init() == ROBOHUB_OK);
    rh_update_info info;
    if (strcmp(name, "write") == 0) {
        rh_update_caps caps;
        rh_update_caps_get(&caps);
        assert(caps.updatable && caps.slot_size > 0u);
        assert(!rh_update_pending(0));
        int fd = open("/proc/self/exe", O_RDONLY);
        assert(fd >= 0);
        struct stat st;
        assert(fstat(fd, &st) == 0);
        uint32_t size = (uint32_t)st.st_size;
        assert(rh_update_begin(size + 1u) == ROBOHUB_OK);   /* begun again below: forgets this */
        assert(rh_update_begin(size) == ROBOHUB_OK);
        uint8_t buf[192], back[192];
        uint32_t at = 0;
        assert(rh_update_write(10u, buf, 1u) == ROBOHUB_ERR_STATE); /* out of order */
        rh_update_info early = { 1u, 1u, 1u };
        while (at < size) {
            uint32_t n = size - at < sizeof buf ? size - at : (uint32_t)sizeof buf;
            assert(read(fd, buf, n) == (ssize_t)n);
            if (at == 0u) {
                assert(rh_update_boot_once(&early) == ROBOHUB_ERR_STATE); /* not whole yet */
            }
            assert(rh_update_write(at, buf, n) == ROBOHUB_OK);
            assert(rh_update_read(at, back, n) == ROBOHUB_OK && memcmp(buf, back, n) == 0);
            at += n;
        }
        close(fd);
        assert(rh_update_write(at, buf, 1u) == ROBOHUB_ERR_INVALID_ARG); /* past the size */
        const char *sec = getenv("TEST_SECURITY");
        info.update_id = (uint32_t)atoi(getenv("TEST_UPDATE_ID"));
        info.firmware_version = 7u;
        info.security_version = sec != 0 ? (uint32_t)atoi(sec) : 0u;
        (void)rh_update_boot_once(&info);
        return 1; /* boot_once returns only when it could not */
    }
    if (strcmp(name, "trial") == 0 || strcmp(name, "confirm") == 0) {
        assert(running_copy());
        assert(rh_update_pending(&info));
        assert(info.update_id == (uint32_t)atoi(getenv("TEST_UPDATE_ID")) && info.firmware_version == 7u);
        assert(rh_update_running_slot() == 1);
        if (strcmp(name, "confirm") == 0) {
            assert(rh_update_confirm() == ROBOHUB_OK);
            assert(!rh_update_pending(0));
            assert(rh_update_confirm() == ROBOHUB_ERR_STATE);
        }
        return 0; /* the trial ends unconfirmed: a crash, a hang the watchdog ended */
    }
    if (strcmp(name, "rollback") == 0) {
        assert(!running_copy());
        assert(!rh_update_pending(0));
        assert(rh_update_running_slot() == 0);
        assert(rh_update_take_outcome(&info) == RH_UPDATE_ROLLED_BACK && info.update_id == 41u);
        assert(rh_update_take_outcome(&info) == RH_UPDATE_NONE);
        assert(rh_update_security_version() == 0u);
        return 0;
    }
    if (strcmp(name, "kept") == 0) {
        assert(running_copy());
        assert(!rh_update_pending(0));
        assert(rh_update_running_slot() == 1);
        assert(rh_update_take_outcome(&info) == RH_UPDATE_KEPT && info.update_id == 42u);
        assert(rh_update_take_outcome(0) == RH_UPDATE_NONE);
        assert(rh_update_security_version() == 3u);
        return 0;
    }
    if (strcmp(name, "reject") == 0) {
        /* Given up on trial: ends to be started again, never returning. */
        assert(running_copy() && rh_update_pending(&info) && info.update_id == 43u);
        (void)rh_update_reject();
        return 1;
    }
    if (strcmp(name, "after-reject") == 0) {
        assert(running_copy() && rh_update_running_slot() == 1);   /* 42's image, as kept */
        assert(!rh_update_pending(0));
        assert(rh_update_take_outcome(&info) == RH_UPDATE_ROLLED_BACK && info.update_id == 43u);
        return 0;
    }
    if (strcmp(name, "again") == 0) {
        /* A later start still runs it; the outcome was taken once. */
        assert(running_copy() && rh_update_running_slot() == 1);
        assert(rh_update_take_outcome(0) == RH_UPDATE_NONE);
        assert(rh_update_security_version() == 3u);
        return 0;
    }
    return 2;
}

// @verifies LNX-0009
int main(int argc, char **argv)
{
    if (argc > 1) {
        return child(argv[1]);
    }
    ssize_t n = readlink("/proc/self/exe", self_path, sizeof self_path - 1);
    assert(n > 0);
    self_path[n] = '\0';
    char dir[] = "/tmp/rh-update-XXXXXX";
    assert(mkdtemp(dir) != 0);
    setenv("RH_UPDATE_DIR", dir, 1);

    setenv("TEST_UPDATE_ID", "41", 1);
    assert(step("write") == 75);
    assert(step("trial") == 0);
    assert(step("rollback") == 0);

    setenv("TEST_UPDATE_ID", "42", 1);
    setenv("TEST_SECURITY", "3", 1);
    assert(step("write") == 75);
    assert(step("confirm") == 0);
    assert(step("kept") == 0);
    assert(step("again") == 0);

    /* An image given up on trial (COMM-0014): written to slot 0 now, started on
       trial, rejected, and the next start runs 42's again. */
    setenv("TEST_UPDATE_ID", "43", 1);
    assert(step("write") == 75);
    assert(step("reject") == 75);
    assert(step("after-reject") == 0);

    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    (void)system(cmd);
    printf("linux update slot: ok\n");
    return 0;
}
