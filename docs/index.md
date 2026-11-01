# RoboHub Linux Platform

**Version:** 0.2.0

The Linux platform for the RoboHub ecosystem: the interfaces declared by
`robohub-common`, implemented on Linux hosts, with board support packages
for the Linux-based boards.

## The platform

A Rho program on this platform runs on a Linux host. The platform provides
the interfaces, the HAL, and the board support package that registers the
board's peripherals. The console is a file descriptor, the clock is
CLOCK_MONOTONIC, and a serial port is a tty.

## Building

```sh
cd examples/c/hello_world
rho build -target uno-q .
```

## Deploying

```sh
rho deploy -target uno-q -run build/output/hello_world
```

## The examples

`examples/c/hello_world` and `examples/rho/hello_world` are the smallest
programs that run on real hardware: they say hello and write a line to the
console every second.

## System dependencies

- `ssh` — for deploying to the board
- `rsync` — for copying the program to the board
- `aarch64-linux-gnu-gcc` — for cross-compiling

## Deployment metadata

The boards this platform carries say what a deployment may make of them and how
the keeper reaches them:

```yaml
capabilities:
  gateway: true
  relay: true
  node: false
  host: true
  wireless: true
  links: "uart console usb wifi socket"
  buses: "uart usb"

deployment:
  method: "adb"
  arch: "arm64"
  kconfig: "CONFIG_RH_BOARD_ARDUINO_UNO_Q=y"
  apps_dir: "/home/arduino/apps"
```

`native` is the machine running the build or the keeper; it is a host with UART,
USB, Ethernet, WiFi and socket links. The UNO Q is a host gateway with WiFi and
USB; its Ethernet is added by the USB-C dongle the board doesn't carry yet. The
keeper prefers ADB for the UNO Q when it is connected over USB, and SSH for a
board reached over the network.
