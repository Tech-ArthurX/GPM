# GUI

Chinese version: [README.md](README.md)

`gui/` is the native Direct2D frontend for GPM.

It organizes package browsing, install queues, progress display, theme switching,
and language switching.

## Main parts

- `main.cpp` - page state, WebSocket client, package list rendering, and user actions.
- `assets/` - frontend images and static resources.
- `lang/` - localization files.
- `themes/` - theme JSON files.
- `ui_d2d/` - native UI runtime and control implementations.

## Build notes

The frontend is usually built with `build_mingw.bat`.
If `g++` or `windres` are not already on `PATH`, set `MINGW_BIN` first.
