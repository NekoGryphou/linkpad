# Steam Link Tool

Run a command by holding Xbox **View + Menu for one second** on Steam Link's home screen. Uses native UI state and controller events; no screenshot detection or Python.

## Build and install

Build on the Steam Link machine with a C/C++17 compiler and Make. Steam Link must have been launched once to download its bundled libraries.

```sh
make headers     # Debian/Raspberry Pi OS: download Qt headers without installing Qt
make test
sudo make install
```

Set `STEAMLINK_DIR` if the bundle is outside `~/.local/share/SteamLink`, or `QT_HEADERS` to use existing headers. Tested on ARM64 Raspberry Pi OS with Valve Qt 5.14.1 and the English home screen.

## Configure

Create `~/.config/steamlink-tool/action.json`:

```json
{
  "command": ["/absolute/path/to/your-tool", "--argument", "value with spaces"]
}
```

Changes apply on the next shortcut—no restart or rebuild. Each array entry is one argument; no shell interprets it. Optional `working_directory` defaults to your home. Commands run asynchronously as your user with the launcher's original library environment. An empty command runs nothing.

Set `STEAMLINK_TOOL_CONFIG` to use another configuration file.

## Run

Close any other client instance, then run:

```sh
steamlink-tool
```

It wraps the official Steam Link launcher. Existing startup services can invoke this command too. Installation does not change services, drivers, LEDs or networking.

## Test and uninstall

```sh
make test-unit    # Chord and shell packaging tests; also works on WSL
make test         # Requires Steam Link's bundled Qt libraries
make test-input   # Optional virtual-controller test; requires /dev/uinput access
sudo make uninstall
```

Stop other client instances before `test-input`. User configuration is preserved on uninstall. `PREFIX`, `DESTDIR` and `OUT_DIR` are supported; `make clean` removes build output.

The repo contains only tool source, build/install scripts and tests. Physical-controller and extended gameplay testing remain outstanding. License undecided.
