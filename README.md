# sysprop

A global key-value property store for Linux embedded systems, modelled after
Android's `getprop`/`setprop`. Processes share properties by reading and writing
named files in a tmpfs directory — no daemon, no IPC, no shared memory.

---

## Property classes

| Key prefix | Behaviour |
|---|---|
| _(none)_ | Volatile — stored in tmpfs; lost on reboot |
| `ro.*` | Read-only — written exclusively by `sysprop-init` at boot; all runtime writes are rejected |
| `persist.*` | Persistent — stored on disk; survives reboots |

Only keys that begin with the literal string `ro.` are read-only. The bare key
`ro` (no dot) is mutable.

## Key format

Keys use only `[a-zA-Z0-9._-]`, with dot-separated non-empty segments — no
leading, trailing, or consecutive dots. Maximum length is 255 bytes.

---

## Boot-time setup

`sysprop-init` must run before any process reads or writes properties. It
creates the runtime directory, removes stale temp files from a previous crash,
and optionally loads a defaults file:

```sh
# Minimal: create the runtime directory only.
sysprop-init

# Load factory defaults (key=value lines; # introduces a comment).
sysprop-init /etc/sysprop/build.prop
```

`persist.*` properties are read directly from the persistent directory on disk —
they do not need to be copied anywhere on boot.

For systemd integration, see [SYSTEMD.md](SYSTEMD.md).

---

## CLI usage

```sh
# Get a property (prints a blank line if not found)
sysprop get ro.build.version
getprop ro.build.version

# Get with a fallback default
sysprop get missing.key "default-value"
getprop missing.key "default-value"

# Set a property
sysprop set device.name my-board
setprop device.name my-board

# Surrounding quotes are stripped before storing
setprop device.name "my-board"

# Delete a property
sysprop delete device.name

# Delete via setprop (empty value = delete)
setprop device.name ""

# List all properties (sorted)
sysprop list
getprop
```

`getprop` and `setprop` are symlinks to `sysprop`.

`ro.*` properties are written exclusively by `sysprop-init` at boot. Any call to
`sysprop set` on an `ro.*` key is rejected, even if the property does not yet
exist. `persist.*` properties are written to and read from the persistent
directory on disk.

---

For library integration and CMake setup, see [INSTALL.md](INSTALL.md).
