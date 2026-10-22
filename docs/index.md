# RoboHub Linux Platform

The Linux platform for the RoboHub ecosystem: the interfaces declared by
`robohub-common`, implemented on Linux hosts, with board support packages
for the Linux-based boards.

## The platform

A Rho program on this platform runs on a Linux host. The platform provides
the interfaces, the HAL, and the board support package that registers the
board's peripherals.

## Building

```sh
cd examples/c/hello_world
rho build -target arduino-uno-q .
```

## Deploying

```sh
rho deploy -target arduino-uno-q -run build/gateway
```

## The examples

`examples/c/hello_world` is the smallest program that runs on real hardware.

`examples/rho/hello_world` is that program in Rho, written against the
contracts and nothing else.
