# RoboHub Linux Platform

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
