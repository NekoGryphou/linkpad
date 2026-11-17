# Linkpad

Run commands from Steam Link's home screen by holding Xbox controller button combinations.

- Multiple shortcuts, each with its own button combination, hold time and commands
- Shortcuts trigger only on the exact Steam Link home screen
- Configuration is reloaded automatically; no restart needed
- Works with the official Steam Link client; no screenshots or polling

## Requirements

- Raspberry Pi OS / Debian (tested on ARM64, Raspberry Pi 3 B+)
- Official Steam Link client, launched once so it has downloaded its bundled Qt 5.14.1
- Xbox controller (Microsoft vendor ID `045e`)
- C/C++17 compiler and Make

## Installation

On the Steam Link machine:

```sh
make headers      # download Qt and nlohmann/json headers (nothing is installed)
make test
sudo make install
```

This installs `/usr/local/bin/linkpad` and `/usr/local/lib/linkpad/`. Services, drivers and system configuration are not modified.

## Usage

Run the tool instead of the official launcher:

```sh
linkpad
```

To start Steam Link at boot, point your existing startup service at `linkpad`.

## Configuration

Shortcuts are read from `~/.config/linkpad/shortcuts.json`:

```json
{
  "shortcuts": [
    {
      "buttons": ["view", "menu"],
      "hold_ms": 1000,
      "commands": [
        ["/usr/local/bin/first-tool", "--argument", "value with spaces"],
        ["/usr/local/bin/second-tool"]
      ]
    },
    {
      "buttons": ["view", "y"],
      "working_directory": "/home/steamlink/scripts",
      "commands": [["./backup.sh"]]
    }
  ]
}
```

| Key | Required | Description |
| --- | --- | --- |
| `buttons` | yes | Buttons held together on one controller: `a`, `b`, `x`, `y`, `view`, `menu`, `guide`, `lb`, `rb`, `ls`, `rs`. Each combination may be used once. |
| `hold_ms` | no | Hold time in milliseconds, 100–60000. Default: `1000`. |
| `commands` | yes | Commands to start, in order, without waiting for each other. Each command is an argument array; no shell is involved. |
| `working_directory` | no | Absolute path the commands start in. Default: your home directory. |

Notes:

- Every combination must include `view`, `menu` or `guide` (see [Controller behaviour](#controller-behaviour)).
- A command without `/` is looked up in `PATH`. Relative paths start from `working_directory`.
- Scripts need a `#!` line (for example `#!/bin/sh`) and execute permission.
- If the file is invalid, all shortcuts are disabled and the error is logged.
- If one command fails to start, the others still start.

### Command environment

Commands run as the same user, in their own session, with standard input from `/dev/null`. Their output goes to the client's log. Steam Link's changes to `PATH`, `LD_LIBRARY_PATH`, `LD_PRELOAD`, `QT_PLUGIN_PATH`, `QT_QPA_PLATFORM` and `TMPDIR` are reverted, and `LINKPAD_*` variables are removed.

## Controller behaviour

- A combination must be held exactly: no extra buttons, on a single controller.
- After a shortcut fires, all buttons must be released before it can fire again.
- On the home screen, View, Menu and Guide are not passed to Steam Link while the held buttons can still become a combination. Pressing View alone otherwise quits Steam Link.
- While a full combination is held, no controller buttons are passed to Steam Link.
- A, B, X, Y, bumpers and sticks behave normally until a combination is complete.

## Environment variables

| Variable | Used by | Description |
| --- | --- | --- |
| `LINKPAD_CONFIG` | runtime | Path of the configuration file. |
| `XDG_CONFIG_HOME` | runtime | Base directory for the default configuration path. |
| `LINKPAD_CLIENT` | runtime | Steam Link launcher to run. Default: `/usr/bin/steamlink`. |
| `LINKPAD_RUNTIME_DIR` | runtime | Directory for runtime state. Default: a temporary directory. |
| `STEAMLINK_DIR` | build | Steam Link bundle. Default: `~/.local/share/SteamLink`. |
| `QT_HEADERS`, `JSON_HEADERS` | build | Use existing Qt 5 or nlohmann/json headers instead of `make headers`. |
| `PREFIX`, `DESTDIR` | install | Installation prefix (default `/usr/local`) and staging directory. |
| `OUT_DIR`, `CC`, `CXX` | build | Build output directory and compilers. |

## Logs

Messages are written to the client's standard error with the prefix `LINKPAD`. Under systemd:

```sh
journalctl -u steamlink.service | grep LINKPAD
```

## Troubleshooting

- **The extension is disabled after an error.** When `LINKPAD_RUNTIME_DIR` is set and the client exits with an error, later starts run the client without the extension until that directory is cleared (for example, by rebooting when it is under `/run`).
- **Disable the extension completely.** Create `/etc/linkpad.disabled`.
- **Unsupported Qt version.** The extension only loads into Steam Link's Qt 5.14.1; it logs `LINKPAD disabled` otherwise.

## Testing

```sh
make test-unit    # no Qt needed; runs on a PC after `make headers-json`
make test         # needs Steam Link's bundled Qt libraries
make test-input   # virtual controller; needs /dev/uinput access and no other client running
```

## Uninstall

```sh
sudo make uninstall
```

User configuration is kept. `make clean` removes build output.

## License

[MIT](LICENSE)
