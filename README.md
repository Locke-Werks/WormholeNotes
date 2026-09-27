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

Every app is a place, and every site in a browser is its own place, told
apart by the host in the address bar. Switch windows and the hole moves to
the new window with that place's notes behind it.

## Driving it

| Do this | Get this |
| --- | --- |
| Click the hole | The note for this place opens out of it |
| Drag the hole | It moves; this place remembers where |
| Drag the binder clip round the rim, or scroll over the rim | The note turns to another open place |
| The arrows in the middle of the note | The next or previous page, then sheet; past the last, a new sheet |
| The pushers on the rim: plus, minus, colour, dot | New sheet, delete sheet, the sheet's ring colour, put away |
| Escape, or switch apps | The note folds back into the hole |
| Right-click the note | Edit commands, About, Quit |
| Tray menu > Show Desk Notes | Desk notes come up above your windows until you switch apps |
| Drag the rim, or its outer edge | The note moves, or the circle resizes |

A hole with something written behind it glows. An empty one is dark. On the
rim, a notch marks every open place, bright where something is written, in
the same order as the taskbar.

The writing runs along one spiral groove, from the rim at noon, clockwise,
in towards the middle, like a record. A new paragraph leaves a dot in the
groove rather than breaking it. When the spiral reaches the middle the page
is full and the next one starts again at the rim. Up and Down move a turn out
or in.

Each sheet has a ring colour, one of the six house colours: cyan, violet,
blue, magenta, crimson or ember. It lights the rim, the caret and the hole,
and marks the place's notch on the ring. The colour pusher steps through
them; the right-click menu has them by name.

A place can hold several sheets, one thought each. Blank sheets are never
kept and close with their page.

Sheets are stored locally in `%LOCALAPPDATA%\Locke Werks\WormholeNotes\sheets.json`.
Nothing is synced.

Drag a note by its rim, or drag a hole with writing behind it, and a Tear off
target appears at the foot of the screen; drop it there and the sheet becomes a
small round note on the desktop. Right-click a note for Tear Off to Desktop,
too. Desk notes sit under your windows; Show Desk Notes in the tray menu brings
them up, and dragging one onto any window hands its writing to that place.

## Browser extension

Without it, WormholeNotes reads the page from the address bar. With it, the
browser says exactly which page each window shows and which tabs are open.
It sends URLs and titles to WormholeNotes on this machine and nowhere else.

In Chrome, Edge or Brave: open the extensions page, turn on Developer mode,
choose Load unpacked, and pick the `extension` folder in the install
directory. WormholeNotes registers itself with those browsers when it starts.

## Requirements

Windows 11, x64. Windows 10 1809 or later should work.

## Building

Qt 6.8 (MSVC 2022 kit) and CMake 3.21 or newer.

```
cmake --preset vs -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset release
```

## Credit

Inspired by [Tondo](https://github.com/Locke-Werks/tondo), Archon's notepad in
the round.

## License

GPLv3. See [LICENSE](LICENSE).
