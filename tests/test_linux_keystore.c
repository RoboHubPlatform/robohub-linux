/*
 * test_linux_keystore.c — the host keystore: a 0600 file, replaced atomically,
 * found through the environment, and absent when nothing names it.
 *
 * Works in a fresh directory under $TMPDIR (or /tmp) and removes it afterwards.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#define _DEFAULT_SOURCE
#include "robohub/keystore.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static rh_key_t make_key(uint32_t epoch)
{
    rh_key_t k;
    k.epoch = epoch;
    for (int i = 0; i < 8; i++) k.scope[i] = (uint8_t)(0x30u + epoch + (uint32_t)i);
    for (int i = 0; i < 16; i++) k.key[i] = (uint8_t)(0xA0u + epoch + (uint32_t)i);
    return k;
}

static void expect_key(uint32_t epoch)
{
    rh_key_t got, want = make_key(epoch);
    assert(rh_keystore_load(&got) == 0);
    assert(got.epoch == epoch);
    assert(memcmp(got.scope, want.scope, 8) == 0);
    assert(memcmp(got.key, want.key, 16) == 0);
}

int main(void)
{
    const char *base = getenv("TMPDIR");
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/rh-keystore-XXXXXX", (base && base[0]) ? base : "/tmp");
    assert(mkdtemp(dir) != 0);

    char file[600], tmp[620], keyfile[600], sibling[620], statefile[600];
    snprintf(file, sizeof(file), "%s/ks.bin", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", file);
    snprintf(keyfile, sizeof(keyfile), "%s/device.key", dir);
    snprintf(sibling, sizeof(sibling), "%s.keystore", keyfile);
    snprintf(statefile, sizeof(statefile), "%s/keystore.bin", dir);

    unsetenv("RH_KEYSTORE_FILE");
    unsetenv("RH_STATE_DIR");
    unsetenv("RH_SECURE_KEY_FILE");

    /* Nothing names a store: the weak default's answers. */
    rh_key_t k = make_key(1), none;
    assert(rh_keystore_load(&none) == -1);
    assert(rh_keystore_store(&k) == -1);

    /* An explicit file. */
    setenv("RH_KEYSTORE_FILE", file, 1);
    assert(rh_keystore_load(&none) == -1); /* not there yet */
    assert(rh_keystore_store(&k) == 0);
    expect_key(1);
    struct stat st;
    assert(stat(file, &st) == 0);
    assert((st.st_mode & 0777) == 0600);
    assert(st.st_size == 80);
    assert(access(tmp, F_OK) != 0); /* renamed, nothing left behind */

    rh_key_t k2 = make_key(2), k3 = make_key(3);
    assert(rh_keystore_store(&k2) == 0);
    expect_key(2);
    assert(rh_keystore_store(&k3) == 0);
    expect_key(3);

    /* A truncated file (torn by something other than this code) keeps whatever
     * record is still whole. */
    assert(truncate(file, 50) == 0); /* slot A (key 3) whole, slot B (key 2) torn */
    expect_key(3);
    assert(truncate(file, 10) == 0);
    assert(rh_keystore_load(&none) == -1);

    /* A stale temporary from a crash does not stop the next write. */
    FILE *f = fopen(tmp, "w");
    assert(f != 0);
    fputs("junk", f);
    fclose(f);
    assert(rh_keystore_store(&k) == 0);
    expect_key(1);
    assert(access(tmp, F_OK) != 0);

    /* The state directory, then the key file's sibling. */
    unsetenv("RH_KEYSTORE_FILE");
    setenv("RH_STATE_DIR", dir, 1);
    assert(rh_keystore_store(&k2) == 0);
    assert(access(statefile, F_OK) == 0);
    expect_key(2);
    unsetenv("RH_STATE_DIR");
    setenv("RH_SECURE_KEY_FILE", keyfile, 1);
    assert(rh_keystore_store(&k3) == 0);
    assert(access(sibling, F_OK) == 0);
    expect_key(3);
    unsetenv("RH_SECURE_KEY_FILE");

    unlink(file);
    unlink(statefile);
    unlink(sibling);
    rmdir(dir);
    printf("linux keystore: ok\n");
    return 0;
}
