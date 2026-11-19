# Changelog

All notable changes to this module are recorded here, one section per release.

## [0.2.0] - 2026-11-01

The UNO Q manifest declares the host gateway role, WiFi and UART/USB links, and
an ADB/arm64 deployment to `/home/arduino/apps`; the documentation explains the
keeper's ADB deployment and that Ethernet is added with the USB-C dongle.

## [0.3.0] - 2026-11-14

The board key is kept in a 0600 file replaced atomically. Linux hosts have no production mode.

## [0.3.1] - 2026-11-19

The UNO Q's start command was `{artifact}` in `target.json` and `cd {artifact} && ./{artifact}` in the board manifest. `rho deploy` reads `target.json` (through rho-target) and replaces `{artifact}` with the full path the program was copied to, so `{artifact}` is right and the manifest's form would have run `cd` into a file; the manifest now says `{artifact}` too, and so does the runbook. The container defaults combined `--net host` with `-p 8080:8080`, and docker discards a port mapping under host networking: the mapping is gone and the runbook says why the gateway runs on the host's network (its REST port, its uplink port and its UDP collector are the board's own). New suite `uno-q-deploy` (`tests/uno_q_deploy.py`) keeps the two deploy blocks the same and the docker defaults free of `-p`.

0.2.0 called the UNO Q's deployment ADB, and the documentation said the keeper prefers ADB over USB and SSH over the network. There are two: `rho deploy` copies with rsync over SSH from the target spec's `deploy` block, and RoboHub Mesh (not the keeper) pushes the gateway over ADB from the manifest's `deployment` block, without falling back to SSH. The documentation now says which tool uses which.
