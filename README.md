# Steam Link Tool

Run commands by holding Xbox controller button combinations on Steam Link's home screen. Uses native UI state and controller events; no screenshot detection, polling or Python.

## Build and install

Build on the Steam Link machine with a C/C++17 compiler and Make. Steam Link must have been launched once to download its bundled libraries.

```sh
make headers     # Debian/Raspberry Pi OS: download Qt and nlohmann/json headers without installing them
make test
sudo make install
```

Set `STEAMLINK_DIR` if the bundle is outside `~/.local/share/SteamLink`, or `QT_HEADERS` / `JSON_HEADERS` to use existing headers. Tested on ARM64 Raspberry Pi OS with Valve Qt 5.14.1 and the English home screen.

## Configure

Create `~/.config/steamlink-tool/shortcuts.json`:

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

- `buttons`: `a`, `b`, `x`, `y`, `view`, `menu`, `guide`, `lb`, `rb`, `ls`, `rs`. Exactly these buttons must be held on one controller; each combination may appear once.
- `hold_ms`: 100–60000, default 1000.
- `commands`: started in order, without waiting for each other. A command that cannot start is logged and the others still start. Each array entry is one argument; no shell interprets it.
- `working_directory`: absolute path, default your home.

Changes apply on the next button press; no restart. An invalid file disables all shortcuts and logs the reason. Commands run as your user with the launcher's original environment. Set `STEAMLINK_TOOL_CONFIG` to use another file.

On the home screen, Steam Link does not receive View, Menu or Guide while the held buttons can still become one of your combinations (View alone would otherwise quit Steam Link). Once a combination is fully held, Steam Link ignores controller buttons until all are released. A, B, X, Y, bumpers and sticks keep their normal function until a combination is complete, so include View, Menu or Guide in every combination.

## Run

Close any other client instance, then run:

```sh
steamlink-tool
```

It wraps the official Steam Link launcher. Existing startup services can invoke this command too. Installation does not change services, drivers, LEDs or networking. Messages are prefixed `STEAMLINK_TOOL` in the client's log.

## Test and uninstall

```sh
make test-unit    # Tests without Qt; also run on a PC after make headers-json
make test         # Requires Steam Link's bundled Qt libraries
make test-input   # Virtual-controller test; requires /dev/uinput access
sudo make uninstall
```

Stop other client instances before `test-input`. User configuration is preserved on uninstall. `PREFIX`, `DESTDIR` and `OUT_DIR` are supported; `make clean` removes build output.

The repo contains only tool source, build/install scripts and tests. License undecided.
