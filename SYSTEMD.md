# sysprop on systemd — Embedded System Setup Guide

This guide explains how to integrate `sysprop-init` and the `sysprop` CLI into a
systemd-based embedded Linux system so that system properties are available to all
services from early boot onward.

---

## How It Works

There are two storage locations:

| Directory | Default path | Filesystem | Survives reboot? | Holds |
|---|---|---|---|---|
| **Runtime dir** | `/run/sysprop/props` | tmpfs (`/run`) | No | Volatile and `ro.*` properties |
| **Persistent dir** | `/etc/sysprop/persistent` | rootfs | Yes | `persist.*` properties |

`/run` is a tmpfs that systemd mounts automatically during early boot. Everything
under it is wiped on every reboot. This means:

- **At every boot**, `sysprop-init` must run before any service that reads or
  writes properties. It re-creates the runtime directory, cleans up stale temp
  files from a previous crash, and optionally loads a defaults file.
- **`ro.*` and volatile properties** from the defaults file are re-set on every
  boot because the runtime tmpfs is cleared on reboot.
- **`persist.*` properties** are stored on disk in the persistent directory.
  Processes read and write them there directly — they are never copied to the
  runtime directory.

---

## Directory Layout

```
/run/sysprop/props/          ← runtime dir (tmpfs; created by sysprop-init)
    device.name              ← volatile property
    ro.build.version         ← read-only property

/etc/sysprop/persistent/     ← persistent dir (on disk; created by sysprop-init)
    persist.wifi.ssid        ← persistent property

/etc/sysprop/build.prop      ← optional: factory defaults file (key=value)
```

Do not write files into these directories by hand. Always use `sysprop-init` or
the `sysprop` CLI.

---

## Step 1 — Install the Binaries

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /usr
```

This installs:
- `/usr/bin/sysprop` — the main CLI
- `/usr/bin/sysprop-init` — the boot-time initializer
- `/usr/bin/getprop` → symlink to `sysprop`
- `/usr/bin/setprop` → symlink to `sysprop`

---

## Step 2 — Create the sysprop-init Service Unit

The runtime and persistent directory paths are baked into the binary at build
time. The only runtime argument accepted is an optional defaults file path.

Create `/etc/systemd/system/sysprop-init.service`:

```ini
[Unit]
Description=System Property Store Initializer
DefaultDependencies=no
After=local-fs.target
Before=sysinit.target

[Service]
Type=oneshot

# RemainAfterExit=yes lets other units use "After=sysprop-init.service"
# reliably — systemd treats the service as active after the process exits.
RemainAfterExit=yes

# Optionally pass a defaults file. Remove the argument if you have no build.prop.
# If the file is specified but does not exist, sysprop-init exits with an error.
ExecStart=/usr/bin/sysprop-init /etc/sysprop/build.prop

# Do not restart on failure — a broken property store at boot is a hard
# fault that requires operator attention.
Restart=no

[Install]
WantedBy=sysinit.target
```

Enable it so it runs on every boot:

```bash
systemctl enable sysprop-init.service
```

---

## Step 3 — Make Other Services Depend on It

Any service that reads or writes system properties must start after
`sysprop-init.service`. Add to your service unit:

```ini
[Unit]
After=sysprop-init.service
Requires=sysprop-init.service
```

Use `Requires=` if your service cannot function without properties. Use `Wants=`
if property availability is optional (best-effort).

**Example — a network service that reads its config from properties:**

```ini
[Unit]
Description=My Network Daemon
After=network.target sysprop-init.service
Requires=sysprop-init.service

[Service]
Type=simple
ExecStartPre=/bin/sh -c 'sysprop get net.hostname | xargs hostname'
ExecStart=/usr/sbin/my-network-daemon
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

---

## Step 4 — Create a build.prop Defaults File (Optional)

`build.prop` is a plain text file with one `key=value` per line. Lines starting
with `#` are comments. Leading whitespace is ignored. `sysprop-init` sets these
properties at boot, making them available for the lifetime of the boot session.

Create `/etc/sysprop/build.prop`:

```
# Factory defaults — do not edit at runtime.

ro.build.version=1.0.0
ro.build.date=2025-01-01
ro.hw.board=my-board-v2
ro.hw.sku=standard

# Mutable defaults (can be overridden at runtime)
net.hostname=embedded-device
sys.log.level=info
```

Key naming rules:
- Allowed characters: `a–z A–Z 0–9 . _ -`
- No leading/trailing dots, no consecutive dots
- Maximum length: 255 bytes for both key and value
- `ro.*` — written only by `sysprop-init` from `build.prop`; all runtime writes (including the very first) are rejected
- `persist.*` — stored on disk; survives reboots
- Everything else — volatile; lost on reboot

> **Note:** `ro.*` properties live in the runtime tmpfs, which is cleared on
> every reboot. They must be listed in `build.prop` so that `sysprop-init`
> re-populates them on every boot. Once written by `sysprop-init`, they are
> immutable for the lifetime of that boot — no process (including root) can
> overwrite or delete them via the `sysprop` API.

---

## Using the CLI

### Read a property

```bash
sysprop get ro.build.version       # prints value, or blank line if not found
sysprop get net.hostname unknown   # prints "unknown" if the key does not exist
getprop ro.build.version           # same, using the getprop alias
```

### Write a property

```bash
sysprop set net.hostname my-device
setprop net.hostname my-device     # same, using the setprop alias
```

Attempting to write any `ro.*` property at runtime will fail, even the first time:

```bash
$ sysprop set ro.build.version evil
setprop: failed to set 'ro.build.version': read-only property
```

`ro.*` properties can only be set by `sysprop-init` at boot (from `build.prop`).

### Delete a property

```bash
sysprop delete net.hostname
setprop net.hostname ""    # empty value is equivalent to delete
```

`ro.*` properties cannot be deleted.

### List all properties

```bash
sysprop list    # prints key=value lines, sorted
getprop         # same (getprop with no arguments lists all properties)
```

`persist.*` properties are deleted from the persistent directory on disk.

---

## Reading Properties from a Shell Script Inside a Service

Because `sysprop` is a plain command-line tool, you can use it directly in
`ExecStartPre=` lines or shell scripts called by your service:

```bash
#!/bin/sh
HOSTNAME=$(sysprop get net.hostname "embedded-device")
LOG_LEVEL=$(sysprop get sys.log.level "info")
VERSION=$(sysprop get ro.build.version)

echo "Starting with hostname=$HOSTNAME log_level=$LOG_LEVEL version=$VERSION"
```

You can also write to properties from a service to communicate state:

```bash
# Signal that a service has completed its initialization phase.
sysprop set sys.myservice.ready 1
```

Another service can then check or poll this:

```bash
# Wait until myservice signals it is ready (simple poll with timeout).
for i in $(seq 1 30); do
    [ "$(sysprop get sys.myservice.ready 0)" = "1" ] && break
    sleep 0.1
done
```

---

## Persistent Properties

`persist.*` properties are stored directly in `/etc/sysprop/persistent/` and
read directly from there by every process. They are never copied to the runtime
tmpfs.

```bash
# Set a persistent property — survives reboots.
sysprop set persist.wifi.ssid "MyNetwork"

# Read it back (reads from the persistent directory on disk).
sysprop get persist.wifi.ssid   # → MyNetwork

# After reboot, the property is still readable with no extra boot steps.
sysprop get persist.wifi.ssid   # → MyNetwork
```

If the persistent directory is on a read-only filesystem when `sysprop set` is
called, the write fails and returns an error. The property is not written
anywhere.

---

## Boot Sequence Summary

```
systemd starts
    │
    ├─ local-fs.target         (rootfs mounted read-write)
    │       │
    │       └─ sysprop-init.service
    │               1. mkdir -p /run/sysprop/props
    │               2. mkdir -p /etc/sysprop/persistent  (if persistence enabled)
    │               3. Remove stale .tmp.* files from crashed writers
    │               4. Load /etc/sysprop/build.prop (ro.*, volatile defaults)
    │               └─ exits 0
    │
    ├─ sysinit.target
    │
    ├─ your-service-a.service  (After=sysprop-init.service)
    │       reads volatile/ro.*: sysprop get ro.hw.board
    │       reads persist.*:     sysprop get persist.wifi.ssid  (from disk)
    │
    └─ your-service-b.service  (After=sysprop-init.service)
            reads: sysprop get net.hostname
            writes: sysprop set sys.service-b.ready 1
```

---

## Troubleshooting

**`sysprop: I/O error` when reading properties**

The runtime directory does not exist. `sysprop-init` has not run yet or it
failed. Check:

```bash
systemctl status sysprop-init.service
journalctl -u sysprop-init.service
ls /run/sysprop/props
```

**`ro.*` property not appearing after reboot**

`ro.*` properties are volatile — they live in tmpfs and are cleared on every
reboot. They must be listed in `/etc/sysprop/build.prop` so that `sysprop-init`
re-sets them on every boot.

**`persist.*` property not surviving reboot**

Check that `/etc/sysprop/persistent/` is writable and that the property file is
present there:

```bash
ls -la /etc/sysprop/persistent/
```

If the file is missing, the write to the persistent backend failed. Check stderr
output from the process that called `sysprop set`.

**My service starts before properties are ready**

Ensure your unit has both lines:

```ini
After=sysprop-init.service
Requires=sysprop-init.service
```

Without `Requires=`, systemd will not enforce ordering if `sysprop-init.service`
is not in the dependency graph of the target being reached.
