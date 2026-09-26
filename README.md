<div align="center">

<img src="assets/wormholenotes.ico" width="96" alt="WormholeNotes">

# WormholeNotes

**A round sticky note that tunnels through every window.**

[![license](https://img.shields.io/badge/license-GPLv3-d6262a?style=flat-square)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Windows%2011-d6262a?style=flat-square)](#requirements)

</div>

---

High school note cards came hole-punched in the top corner with a binder clip
through them. WormholeNotes is that hole: a small punched circle that sits on
whatever window you are using and opens into a note about it.

Every app is a place, and every page in a browser is its own place. Each place
has one sheet. Switch windows and the hole moves to the new window with that
place's sheet behind it.

## Driving it

| Do this | Get this |
| --- | --- |
| Click the hole | The note for this place opens out of it |
| Drag the hole | It moves; this place remembers where |
| Escape, the close button, or switch apps | The note folds back into the hole |
| Click the tray icon | The note for the current place |
| Note > Quit, or the tray menu | WormholeNotes exits |

A hole with something written behind it glows. An empty one is dark.

Sheets are stored locally in `%LOCALAPPDATA%\Locke Werks\WormholeNotes\sheets.json`.
Nothing is synced.

## Status

Early. Browser pages are told apart by window title for now, which shifts as
pages load. Reading the address bar and a browser extension come next, along
with a ring of open places on the bezel, several notes per place, and tearing a
note off onto the desktop.

## Requirements

Windows 11, x64. Windows 10 1809 or later should work.

## Building

Qt 6.8 (MSVC 2022 kit) and CMake 3.21 or newer.

```
cmake --preset vs -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset release
```

## Credit

The round window, the ring text layout, the radial menus and prompts are from
[Tondo](https://github.com/Locke-Werks/tondo) by Archon. WormholeNotes began as
a copy of it.

## License

GPLv3. See [LICENSE](LICENSE).
