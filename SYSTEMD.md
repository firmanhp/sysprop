# sysprop on systemd — Embedded System Setup Guide

This guide explains how to integrate `sysprop-init` and the `sysprop` CLI into a
systemd-based embedded Linux system so that system properties are available to all
services from early boot onward.

---

## How It Works at a Glance

There are two storage locations:

| Directory | Default path | Filesystem | Survives reboot? | Purpose |
|---|---|---|---|---|
| **Runtime dir** | `/run/sysprop/props` | tmpfs (`/run`) | No | Live read/write store for all processes |
| **Persistent dir** | `/etc/sysprop/persistent` | rootfs | Yes | Long-term storage for `persist.*` properties |

`/run` is a tmpfs that systemd mounts automatically during early boot. Everything
under it is wiped on every reboot. This means:

- **At every boot**, `sysprop-init` must run before any service that reads properties.
- It re-creates the runtime directory, cleans up any leftover temp files from a
  previous crash, and copies persisted properties back into the runtime store.
- After `sysprop-init` exits, the runtime store is ready and all `sysprop` operations
  are fast in-memory file reads with no I/O to persistent storage.

---

## Directory Layout

```
/run/sysprop/props/          ← runtime dir (created by sysprop-init at boot)
    net.hostname             ← a volatile property file
    persist.wifi.ssid        ← a persist.* property (copy of what's on disk)
    ro.build.version         ← a read-only property

/etc/sysprop/persistent/     ← persistent dir (survives reboots)
    persist.wifi.ssid        ← source of truth for persist.* properties

/etc/sysprop/build.prop      ← optional: factory defaults file (key=value)
```

You do not manage these files directly. Always use `sysprop-init` or the `sysprop`
CLI. Writing files into these directories by hand is only appropriate for
provisioning at factory time.

---

## Step 1 — Install the Binaries

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSYSPROP_BUILD_TOOLS=ON
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

Create `/etc/systemd/system/sysprop-init.service`:

```ini
[Unit]
Description=System Property Store Initializer
# Run after the filesystem is mounted read-write but before any service
# that depends on properties. syss-init.target is a good anchor point.
DefaultDependencies=no
After=local-fs.target
Before=sysinit.target

[Service]
Type=oneshot

# The service runs once and exits. RemainAfterExit=yes lets other units
# use "After=sysprop-init.service" reliably — systemd considers the service
# "active" even after the process has exited.
RemainAfterExit=yes

ExecStart=/usr/bin/sysprop-init \
    --runtime-dir    /run/sysprop/props \
    --persistent-dir /etc/sysprop/persistent \
    /etc/sysprop/build.prop

# If build.prop does not exist yet (fresh device), sysprop-init exits with
# an error for that step but the service should still succeed. Remove the
# defaults file argument above if you have no build.prop.

# Ensure the persistent directory exists on first boot.
ExecStartPre=/usr/bin/mkdir -p /etc/sysprop/persistent

# Do not restart on failure — a broken property store at boot is a hard
# fault that requires operator attention, not an automatic retry.
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

Use `Requires=` if your service cannot function at all without properties.
Use `Wants=` instead if property availability is optional (best-effort).

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
with `#` are comments. `sysprop-init` loads it after persistent properties, so
persistent values take precedence over the defaults here.

Create `/etc/sysprop/build.prop`:

```
# Factory defaults — do not edit at runtime.
# Changes here take effect on next boot.

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
- No leading/trailing dots, no consecutive dots (`..`)
- Maximum length: 255 characters for both key and value
- `ro.*` — read-only after first set; any later attempt to change them fails
- `persist.*` — written to both the runtime and persistent store; survives reboots
- Everything else — volatile; lost on reboot

> **Note:** `ro.*` properties in `build.prop` are set during `sysprop-init` and then
> become immutable for the lifetime of that boot. They cannot be changed even by root
> without rebooting.

---

## Using the CLI

### Read a property

```bash
sysprop get ro.build.version        # prints value, or blank if not found
sysprop get net.hostname unknown    # prints "unknown" if the key does not exist
getprop ro.build.version            # same, using the getprop alias
```

### Write a property

```bash
sysprop set net.hostname my-device
setprop net.hostname my-device      # same, using the setprop alias
```

Attempting to overwrite a `ro.*` property will fail:

```bash
$ sysprop set ro.build.version evil
sysprop: failed to set 'ro.build.version': read-only property
```

### Delete a property

```bash
sysprop delete net.hostname
```

`ro.*` properties cannot be deleted. `persist.*` properties are deleted from both
the runtime and persistent stores.

### List all properties

```bash
sysprop list
getprop          # no arguments — same output
```

Output format (Android-compatible):

```
[net.hostname]: [my-device]
[ro.build.version]: [1.0.0]
[persist.wifi.ssid]: [HomeNetwork]
```

---

## Reading Properties from a Shell Script Inside a Service

Because `sysprop` is a plain command-line tool, you can use it directly in
`ExecStartPre=` lines or in shell scripts called by your service:

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

Another service can then poll or check this:

```bash
# Wait until myservice signals it is ready (simple poll with timeout).
for i in $(seq 1 30); do
    [ "$(sysprop get sys.myservice.ready 0)" = "1" ] && break
    sleep 0.1
done
```

---

## Persistent Properties

`persist.*` properties are automatically saved to `/etc/sysprop/persistent/` when
written, and loaded back into the runtime store by `sysprop-init` at next boot.

```bash
# Set a persistent property — survives reboots.
sysprop set persist.wifi.ssid "MyNetwork"
sysprop set persist.wifi.psk  "secret"

# After reboot, sysprop-init will restore these automatically.
sysprop get persist.wifi.ssid   # → MyNetwork
```

If the persistent directory is on a read-only filesystem when `sysprop set` is
called, the write to `/etc/sysprop/persistent/` will fail silently — the property
is still set in the runtime store for the current boot, but will not survive
rebooting. A warning is printed to stderr.

---

## Overriding Directories at Runtime

You can override both directories via environment variables without recompiling.
This is useful for development and testing:

```bash
# Point to custom directories for a single invocation
SYSPROP_RUNTIME_DIR=/tmp/myprops \
SYSPROP_PERSISTENT_DIR=/tmp/mypersist \
    sysprop-init /tmp/my-build.prop

# Use the same overrides when reading back
SYSPROP_RUNTIME_DIR=/tmp/myprops sysprop list
```

The priority order for directory configuration is:
1. `--runtime-dir` / `--persistent-dir` flags (highest priority)
2. `SYSPROP_RUNTIME_DIR` / `SYSPROP_PERSISTENT_DIR` environment variables
3. Compiled-in defaults (`/run/sysprop/props` and `/etc/sysprop/persistent`)

---

## Boot Sequence Summary

```
systemd starts
    │
    ├─ local-fs.target         (rootfs mounted read-write)
    │       │
    │       └─ sysprop-init.service
    │               1. mkdir -p /run/sysprop/props
    │               2. Remove stale .tmp.* files from crashed writers
    │               3. Copy persist.* from /etc/sysprop/persistent → runtime dir
    │               4. Load /etc/sysprop/build.prop (ro.*, defaults, ...)
    │               └─ exits 0
    │
    ├─ sysinit.target
    │
    ├─ your-service-a.service  (After=sysprop-init.service)
    │       reads: sysprop get ro.hw.board
    │
    └─ your-service-b.service  (After=sysprop-init.service)
            reads: sysprop get net.hostname
            writes: sysprop set sys.service-b.ready 1
```

---

## Troubleshooting

**`sysprop: I/O error` when reading properties**

The runtime directory does not exist. Either `sysprop-init` has not run yet, or
it failed. Check:

```bash
systemctl status sysprop-init.service
journalctl -u sysprop-init.service
ls /run/sysprop/props
```

**`ro.*` property not appearing after reboot**

`ro.*` properties are volatile unless you also put them in `build.prop` or the
persistent dir. They are not automatically saved. Add them to
`/etc/sysprop/build.prop` to ensure they are re-set on every boot.

**`persist.*` property not surviving reboot**

Check that `/etc/sysprop/persistent/` is writable and that the corresponding file
is present there:

```bash
ls -la /etc/sysprop/persistent/
```

If the file is missing, the write to the persistent backend failed silently. Check
the service logs:

```bash
journalctl -u your-service.service | grep sysprop
```

**My service starts before properties are ready**

Ensure your unit has both lines:

```ini
After=sysprop-init.service
Requires=sysprop-init.service
```

Without `Requires=`, systemd will not enforce ordering if `sysprop-init.service`
is not in the dependency graph of the target being reached.
