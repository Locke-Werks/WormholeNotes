<div align="center">

<img src="assets/wormholenotes.ico" width="96" alt="WormholeNotes">

# WormholeNotes

**A round sticky note that tunnels through every window.**

[![release](https://img.shields.io/github/v/release/Locke-Werks/WormholeNotes?style=flat-square&color=d6262a)](https://github.com/Locke-Werks/WormholeNotes/releases)
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
| Tray menu > Find in Notes, or Win+Shift+F | Search every sheet in every place; Enter opens the note at the match. Ctrl+Alt+Shift+F when another program holds Win+Shift+F |
| Tray menu > Export Notes | Every note in one Markdown file, a section per place |
| Tray menu > Restore Notes | Your notes go back to a day's backup; the notes it replaced are listed first, to undo it |
| Tray menu > Hide Wormhole | Everything off screen for recording or sharing, until you untick it, restarts included |
| Drag the rim, or its outer edge | The note moves, or the circle resizes |

A hole with something written behind it glows. An empty one is dark. On the
rim, a notch marks every open place, bright where something is written, in
the same order as the taskbar.

The writing runs along one spiral groove, like a record. The newest words sit
upright across the top of the rim, and as you keep typing, earlier writing
slides back along the groove towards the middle. A new paragraph leaves a dot
in the groove rather than breaking it. When the groove is full, the oldest
writing moves to the page before. Move the caret back to fix something and the
note turns until that spot is at the top, the right way up.

Each sheet has a ring colour, one of the six house colours: cyan, violet,
blue, magenta, crimson or ember. It lights the rim, the caret and the hole,
and marks the place's notch on the ring. The colour pusher steps through
them; the right-click menu has them by name.

A place can hold several sheets, one thought each. Blank sheets are never
kept and close with their page.

Sheets are stored locally in `%LOCALAPPDATA%\Locke Werks\WormholeNotes\sheets.json`.
Nothing is synced. A dated copy is kept in the `backups` folder beside it each day, the last 14 days,
and Restore Notes in the tray menu brings one back.

Notes are not encrypted. The sheets file, its backups and any export are plain
text that anyone or any program with access to your Windows account can read.
Do not keep passwords or other secrets in them. WormholeNotes comes with no
warranty, as the GPLv3 sets out, and you use it and trust it with your notes
at your own risk.

Drag a note by its rim, or drag a hole with writing behind it, and a Tear off
target appears at the foot of the screen; drop it there and the sheet becomes a
small round note on the desktop. Right-click a note for Tear Off to Desktop,
too. Desk notes sit under your windows; Show Desk Notes in the tray menu brings
them up, and dragging one onto any window hands its writing to that place.

## Installing

Download `WormholeNotes-<version>-Setup.exe` from
[Releases](https://github.com/Locke-Werks/WormholeNotes/releases) and run it.
It installs to Program Files for everyone on the machine, with a Start Menu
shortcut, and starts WormholeNotes at sign-in; setup has a checkbox for each.
Silent install: `/S`, with `/O:start_menu=off`, `/O:sign_in=off` or
`/O:desktop=on` to change the defaults.

The portable zip runs from any folder, with nothing to install.

From v0.2.0 the installer and WormholeNotes.exe are signed by Specter Point
Intelligence, LLC. v0.1.0 is unsigned, so SmartScreen warns the first time:
choose More info, then Run anyway.

Uninstalling from Installed apps removes the program and its browser
registration. Your notes stay in `%LOCALAPPDATA%\Locke Werks\WormholeNotes`.

## Browser extension

Without it, WormholeNotes reads the page from the address bar. With it, the
browser says exactly which page each window shows and which tabs are open.
It sends URLs and titles to WormholeNotes on this machine and nowhere else.

In Chrome, Edge or Brave: open the extensions page, turn on Developer mode,
choose Load unpacked, and pick the `extension` folder in the install
directory. WormholeNotes registers itself with those browsers when it starts.

In Firefox: open `about:debugging#/runtime/this-firefox`, choose Load
Temporary Add-on, and pick `manifest.json` in the `extension` folder. Firefox
drops a temporary add-on when it closes. For a permanent install, open the
signed `-firefox.xpi` from a release in Firefox.

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

<!-- lockewerks-site
tag: Sticky notes per app and site
- A round sticky note that tunnels through every window
- Each app and each website keeps its own notes
- The writing runs along one spiral groove, like a record
- Stored on this machine, never synced, backed up daily
-->
