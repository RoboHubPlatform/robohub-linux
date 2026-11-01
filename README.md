# robohub-linux

The RoboHub platform for a Linux host.

A board's platform brings up clocks and pins; a host's already has them. This
supplies the same contracts over the machine instead of a chip:

| Contract | On a host |
|---|---|
| console | standard output, and non-blocking standard input |
| timer | `CLOCK_MONOTONIC`, delays by `nanosleep` |
| uart | a tty named by `RHO_UART0` / `RHO_UART1`, opened raw at the baud |
| net | POSIX sockets, non-blocking, the contract's timeout as a `select` |
| onewire, i2c, spi, gpio | not registered — a host has none |

A contract a host has no answer for is **absent**, not failed: the platform
registers what it has, and the runtime's hardware smoke reports the rest as
absent, which is the truth about a machine with no OneWire bus.

## Why it exists

It is what makes a **gateway-only mode** run on the machine it aggregates from.
The runtime, the transport, the telemetry bridge and a relay's uplink are a
board's; only the platform beneath them changes. A relay built for this platform
is a program on a server, a container or a workstation — the headless hop of a
tree, or the sender of an uplink — without a board in it.

## Environment

| Variable | Meaning |
|---|---|
| `RHO_UART0` | the tty registered as `uart0` |
| `RHO_UART1` | the tty registered as `uart1` |

## Build and test

```sh
cmake -S . -B build -DRHO_BUILD_HOST_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test brings the platform up as its own host: a clock that moves forward, no
bus a host does not have, and a socket that reaches a loopback peer.

## Boards

A target is described by a spec under `boards/`, one per board. `native` is the
machine the build runs on; `boards/arduino-uno-q/` is a board this platform
reaches across a network and a USB cable.

### Arduino UNO Q

The UNO Q is one board with two halves: a Qualcomm QRB2210 (4x Cortex-A53, 2 GiB
RAM, 16 GiB eMMC) running **Debian**, and an **STM32U585** (Cortex-M33) companion
MCU. Its target spec carries **both** blocks, and each tool reads the one it is
for:

```sh
# The Linux side: an aarch64 program copied over SSH and started.
rho deploy -target uno-q -run build/gateway

# The MCU: firmware written by Arduino's own flasher.
rho flash -target uno-q build/firmware.bin
```

- **deploy** — `rsync` (or `scp`) to `arduino@uno-q.local:/home/arduino/apps`
  and start it. The address is a fact about the bench: `RH_DEPLOY_HOST`,
  `RH_DEPLOY_USER`, `RH_DEPLOY_PATH`, `RH_DEPLOY_PORT` and `RH_DEPLOY_METHOD`
  override the spec. Adding a `docker` block to a project deploys a container
  image instead of a file.
- **flash** — `arduino-flasher-cli` writes firmware to the MCU. The platform
  ships `tools/arduino-flasher-cli/flash`, so the board is reached the same way
  on every machine. rho builds no firmware for the MCU; that is the Arduino
  toolchain's, and this writes what it produced.

Build for the board with the cross compiler (`aarch64-linux-gnu-gcc`, named in
the spec), or natively when the toolchain runs on the board itself.

The full bring-up — first boot, SSH, deploying a program and the gateway
container, and flashing the MCU — is in [docs/uno-q.md](docs/uno-q.md).

## Release history

[CHANGELOG.md](CHANGELOG.md) — the changes in 0.2.0 and earlier releases.
