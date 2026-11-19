#!/usr/bin/env python3
# @file uno_q_deploy.py
# @brief The UNO Q's deploy facts agree between its target spec and its manifest.
# @module robohub-linux
# @project RoboHub Platform
# @author Aurel Miron
# @date 2026
# SPDX-License-Identifier: MIT
#
# `rho deploy` reads target.json (through rho-target); the board manifest repeats
# the deploy block for a reader. The two said different things about how the
# program is started, so this keeps them the same, keeps the start command what
# rho-deploy can run ({artifact} is the full path the program was copied to), and
# keeps the docker defaults free of a port mapping that host networking discards.
#
# @verifies LNX-0006
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BOARD = ROOT / "boards" / "arduino-uno-q"
failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message)
    if not ok:
        failures.append(message)


spec = json.loads((BOARD / "target.json").read_text())
deploy = spec.get("deploy", {})

# The manifest's deploy block: the indented lines under `  deploy:`.
manifest = (BOARD / "module.yml").read_text().splitlines()
block = {}
inside = False
for line in manifest:
    if re.match(r"^  deploy:\s*$", line):
        inside = True
        continue
    if inside:
        if line.strip().startswith("#") or not line.strip():
            continue
        m = re.match(r"^    (\w+):\s*(.*)$", line)
        if not m:
            break
        value = m.group(2).strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        block[m.group(1)] = value

check(bool(block), "the manifest carries a deploy block")
for key in ("method", "host", "port", "user", "path", "run"):
    check(str(deploy.get(key)) == block.get(key),
          f"deploy.{key} is the same in target.json ({deploy.get(key)!r}) and module.yml ({block.get(key)!r})")

check(deploy.get("method") == "rsync", "the program is copied with rsync (over SSH)")
check(deploy.get("run") == "{artifact}",
      "the program is started by the path it was copied to ({artifact} is that full path)")

run = deploy.get("docker", {}).get("run", [])
host_net = any(a in ("host", "--net=host", "--network=host") for a in run)
check(host_net, "the container runs on the host's network")
check(not ("-p" in run or "--publish" in run or any(a.startswith("--publish=") for a in run)),
      "no port is published: docker discards -p under --net host")

if failures:
    print(f"{len(failures)} failed")
    sys.exit(1)
print("all passed")
