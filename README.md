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
