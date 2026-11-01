# Arduino UNO Q — bring-up runbook

Everything to have in place when the board arrives. The board is two halves on
one PCB, and each half is reached a different way:

- a **Qualcomm QRB2210** (4x Cortex-A53, 2 GiB RAM, 16 GiB eMMC) running
  **Debian** — a program is **deployed** to it over SSH and run;
- an **STM32U585** (Cortex-M33) companion — firmware is **flashed** to it with
  Arduino's flasher.

`robohub-linux` describes the board in `boards/arduino-uno-q/target.json`, whose
spec carries a `deploy` block and a `flash` block. `rho deploy` reads the first
and `rho flash` the second; neither knows about the other.

## Before the board: on the workstation

```sh
# The toolchain, with the components that reach a board.
./scripts/check.sh            # build, publish and install every module
rho target show uno-q -json   # the board resolves, and shows deploy + flash

# Cross compiler for the Linux side (or build on the board itself).
sudo apt-get install gcc-aarch64-linux-gnu

# Reaching the board and building a container.
sudo apt-get install rsync openssh-client docker.io

# Flashing the MCU.
# arduino-flasher-cli (Arduino's flasher) must be on PATH.

# There is no board to reach yet: this must print the steps and run nothing.
rho deploy -target uno-q -n build/robohub-gateway
```

If `rho target show uno-q` does not know the board, the `robohub-linux` module
was not republished after it was added: run `./scripts/check.sh`.

## First boot

1. Connect the board to power and to the workstation over **USB-C**.
2. It boots Debian. Find it on the network — by name (`uno-q.local`, if mDNS is
   available) or from its address on the LAN, or over USB networking.
3. Log in: `ssh arduino@uno-q.local` (the board's Debian user is `arduino`).
   Install the key for passwordless SSH, which is what `rsync`/`scp` and `ssh`
   use: `ssh-copy-id arduino@uno-q.local`.
4. If the address is not `uno-q.local`, it is a fact about the bench:
   `export RH_DEPLOY_HOST=192.168.1.50` (and `RH_DEPLOY_USER` if the login
   differs). Nothing in the spec changes.

## Deploying a program to the Linux side

Build for the board (cross compiler named in the spec), or build on the board
itself when the toolchain is installed there:

```sh
# On the workstation, for the board:
rho build -target uno-q .

# Copy it and start it (rsync, then ssh):
rho deploy -target uno-q -run build/output/<program>

# Or copy only, and start it yourself:
rho deploy -target uno-q build/output/<program>
```

The board's `deploy` block copies to `/home/arduino/apps` and starts the program
with `cd {artifact} && ./{artifact}`. `rho deploy -n` prints every step first.

## Deploying the gateway as a container

The gateway is a host program with a `Dockerfile`, so it is deployed as an
image: built here, saved, carried, loaded and started on the board.

```sh
cd <...>/robohub-gateway
rho build .
rho deploy -target uno-q -docker -run build/robohub-gateway

# Then, from anywhere:
curl http://uno-q.local:8080/api/v1/nodes
```

A gateway that reads a board's serial link needs the device even in a container,
and the run arguments are the project's:

```sh
RH_DEPLOY_DOCKER_RUN='--net host --device /dev/ttyUSB0 -p 8080:8080 {image}' \
    rho deploy -target uno-q -docker -run build/robohub-gateway
```

The image name, tag and Dockerfile default from the board's `deploy.docker`
block (`robohub-gateway:latest`, `Dockerfile`) and can be overridden with
`RH_DEPLOY_DOCKER_IMAGE`, `RH_DEPLOY_DOCKER_TAG` and `RH_DEPLOY_DOCKER_FILE`.

## Flashing the companion MCU

rho writes firmware to the STM32U585 with Arduino's flasher; it does not build
it — that is the Arduino toolchain's, and this writes what it produced.

```sh
# Build the MCU firmware with Arduino tooling, then:
rho flash -target uno-q build/firmware.bin
```

The board's `flash` block is `arduino-flasher-cli upload --board uno-q {binary}`,
and the platform ships `tools/arduino-flasher-cli/flash`, which `rho flash`
prefers over a tool of the same name on `PATH` — so the board is reached the
same way on every machine.

## The UNO Q and the keeper

The board's manifest says a deployment reaches the Linux side over ADB when it
is on the USB cable:

```yaml
deployment:
  method: "adb"
  arch: "arm64"
  apps_dir: "/home/arduino/apps"
```

The keeper uses that block to cross-build the gateway for `arm64`, push it to
`/home/arduino/apps` over ADB, and start it. A board reached over the network
uses its `ssh` deployment instead. The board is also a `host` gateway with
`wireless` and `uart`/`usb` links; Ethernet arrives with the USB-C dongle and is
declared then, not assumed now.

## Bring-up order

1. `rho target show uno-q` resolves (the board spec is installed).
2. `rho deploy -target uno-q -n <artifact>` prints the steps you expect.
3. Reach the board: `ssh arduino@uno-q.local` works without a password.
4. Deploy a small program and run it; `rho deploy -target uno-q -run <artifact>`.
5. Deploy the gateway as a container; `curl http://uno-q.local:8080/api/v1/nodes`.
6. Flash the MCU with a firmware you built with Arduino tooling.

## If something is wrong

| Symptom | Look at |
|---------|---------|
| `rho target show uno-q` does not know the board | republish `robohub-linux` (`./scripts/check.sh`) |
| `rho deploy` says the target declares no deploy block | the installed `rho-target` is older than the board spec — republish |
| `rsync`/`ssh` cannot reach the board | the address: `RH_DEPLOY_HOST`, the login, and that the key is installed |
| The container starts but the gateway reads nothing | the serial device is not in the run arguments (`--device`) |
| `rho flash` cannot find the flasher | `arduino-flasher-cli` is not on `PATH`; the platform's `tools/` entry says so |
| The cross build fails | `aarch64-linux-gnu-gcc` is not installed, or build on the board instead |
