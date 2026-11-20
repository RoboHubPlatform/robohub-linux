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

## [0.4.0] - 2026-11-20

The UNO Q declares that it can host a USB device: `capabilities.usb_host: true`
in `boards/arduino-uno-q/module.yml` (board 0.2.0, LNX-0008). RoboHub Mesh places
a USB Zigbee coordinator only under a board that declares `host` and `usb_host`
(robohub ADR-0017), and `buses: usb` never meant a USB host, so the capability is
a new line rather than read from it. The runbook and the platform page say how a
USB device is plugged in — through a powered USB-C hub on the board's one USB-C
port — that host mode is not enabled when the board is powered from 5V or VIN
until the JCTL header is set as Arduino describes, and that the user running the
program must be in `dialout`. `target.json` carries no capabilities and is
unchanged.

## [0.5.0] - 2026-11-20

A host board takes updates (LNX-0009, robohub ADR-0022): robohub-common's update slot over two image files and a state file in `RH_UPDATE_DIR` (or `$RH_STATE_DIR/update`). Booting an image once records it as pending and ends the program (exit 75) so its supervisor starts it again; that start execs the image on trial; a later start that finds it still unconfirmed goes back to the confirmed slot and keeps "rolled back" for the managed layer to report. The board hook is now named on the link (`--undefined=robohub_board_register`): a program that referred to nothing else of the platform's object linked robohub-common's empty weak hook and started with no console or timer. `tests/test_linux_update.c` runs each start as a fresh process, with a copy of itself as the image.
