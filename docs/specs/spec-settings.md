# Settings specification

This file specifies the settings functionality of the launcher.

## Settings view

Settings view can be opened from the main launcher interface by tapping the settings button at the top of the screen.
Tapping `Back` exits the Settings app and returns to the launcher app.

### Settings menu

The following settings are available:

| Setting | Description | Persistence |
|---------|-------------|-------------|
| Brightness | Adjust display brightness (0-100%) | NVS |
| Date & Time | Set current date and time | RTC + NVS |
| About | Show launcher version, device info, SDK version | - |
| System Info | Show SD card capacity, memory usage, battery level | - |
| Developer Mode | Enable/disable developer mode (see spec-development-mode.md) | NVS |
| Web Config | Start/stop web configuration utility (see spec-web-configuration.md) | - |

## Web configuration utility

The launcher includes a web-based configuration utility that allows configuring the device from a browser.

See [spec-web-configuration.md](./spec-web-configuration.md) for full details.
