<div align="center">

<img src="assets/tondo.ico" width="96" alt="Tondo">

# Tondo

**Notepad, rebuilt so that nothing in it is a rectangle.**

[![release](https://img.shields.io/github/v/release/Locke-Werks/tondo?style=flat-square&color=d6262a)](https://github.com/Locke-Werks/tondo/releases)
[![license](https://img.shields.io/badge/license-GPLv3-d6262a?style=flat-square)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Windows%2011-d6262a?style=flat-square)](#requirements)

</div>

---

<p align="center">
  <img src="assets/screenshot-light.png" width="420" alt="Tondo with text running on concentric rings">
  <img src="assets/screenshot-menu.png" width="420" alt="The Format menu open as a ring, with the Font submenu as a second ring inside it">
</p>

Your monitor is a rectangle. Every window on it is a rectangle. Notepad is a
rectangle pretending to be a sheet of paper, which is also a rectangle. At some
point everyone agreed this was fine, and nobody checked with the circle.

Tondo checked with the circle.

A tondo is a Renaissance painting on a round panel. Michelangelo painted one.
Raphael painted several. None of them had to implement word wrap, which is
where this project spent most of its time.

## What you are looking at

**The text.** Every line is a ring. It starts at the seam at twelve o'clock,
runs clockwise all the way round with each letter standing on the tangent, and
when it gets back to noon it steps one ring inward and carries on. It reads like
the lettering on a coin, or a vinyl record that learned to type.

Text in the lower half of the circle is upside down. This is correct. Tilt your
head, rotate your monitor, or make peace with geometry.

**The hub.** When the rings reach the middle, the page is full and the text moves
on to the next page. The hub in the center says which page you are on, because
you will lose track. Click its top half to go back and its bottom half to go
forward. It looks like a record label and does more work than one.

**The menus.** The menu bar is an arc of the bezel, in the upper left, where a
menu bar would be if it had been bent. Click File and File opens as a ring. Point
at a submenu and it opens as a smaller ring inside the first one. Right-click the
text for the edit menu as a ring around the spot you clicked. Right-click the
bezel for all five menus at once, in a ring, naturally.

**The dialogs.** Find, Replace, Go To, About and "Save changes?" are rings as
well: fields along the top arc, buttons along the bottom. When Find opens, the
text shrinks inward to make room for it, like people making space on a bench.
The save question is round now. It is still the same question, and you will
still answer it wrong once.

<p align="center">
  <img src="assets/screenshot-replace.png" width="420" alt="Replace open: fields along the top arc, toggles and buttons along the bottom">
  <img src="assets/screenshot-dark.png" width="420" alt="Dark mode">
</p>

**Dark mode** follows Windows and makes the whole thing look like a record
player. This was not planned. It is staying.

## Driving it

| Do this | Get this |
| --- | --- |
| Click File, Edit, Format, View or Help on the bezel | That menu, as a ring |
| Right-click the text | Undo, cut, copy, paste and friends, as a ring |
| Right-click the bezel | Every menu, as a ring |
| Drag the bezel | The window moves |
| Drag the outermost edge of the bezel | The circle grows or shrinks around its center |
| Double-click the bezel | The largest circle your screen can fit, and back |
| Click the hub | Previous page on top, next page on the bottom |
| Mouse wheel | Turn pages |
| Ctrl + wheel | Zoom |

Up and Down move to the ring outside or inside at the same angle, so the cursor
travels in a straight line toward or away from the hub. Home and End go to the
start and end of the ring. Page Up and Page Down turn a whole page.

In a menu ring, the arrow keys go round, Enter opens or chooses, and Escape backs
out one ring at a time. Typing an item's Notepad access letter picks it. Alt+F,
Alt+E, Alt+O, Alt+V and Alt+H open the menus from the keyboard, and F10 opens
File, same as always.

## It is still Notepad

Underneath the geometry this is a working editor with Notepad's shortcuts: New,
New Window, Open, Save, Save As, Page Setup, Print, Undo, Redo, Cut, Copy, Paste,
Delete, Find, Find Next (F3), Find Previous (Shift+F3), Replace, Go To, Select
All, Time/Date (F5), Font, Zoom and Status Bar.

Files open in UTF-8, UTF-8 with a BOM, UTF-16 in either byte order, or the ANSI
code page, and save back the way they came, line endings included. A file that
arrived with Unix line endings leaves with Unix line endings. Both are
switchable under Format.

Printing puts each page's rings on their own sheet. Your printer will be
confused. Your printer will cope.

## Known issues

- The bottom half of every page is upside down, as covered above. Filed under
  working as intended.
- A long line of code comes out as a spiral galaxy. There are no plans to fix
  this, since it looks great.
- Maximized, the circle is as big as the screen is tall, which on a widescreen
  monitor still leaves a lot of rectangle either side. That is between you and
  your monitor.
- Three things are still rectangles: the Open and Save dialogs, Print and Page
  Setup, and More Fonts. Windows draws those, and Windows declined to be
  involved.

## Installing

Grab `Tondo-<version>-Setup.exe` from
[Releases](https://github.com/Locke-Werks/tondo/releases). It installs to
Program Files and offers a Start Menu shortcut, a desktop shortcut, and an
"Open with" entry for `.txt` files. It never takes your default editor away from
whatever has it now. Silent install is `/S`, and any option can be turned off
with `/O:start_menu=off`, `/O:desktop=off` or `/O:open_with=off`.

There is also a portable zip if installers make you nervous. Unzip it anywhere
and run `tondo.exe`.

Both are signed. The publisher reads Specter Point Intelligence, LLC, which is
the parent company of Locke Werks.

## Requirements

Windows 10 version 1809 or later, x64. Everything else ships in the box,
including Qt and the Visual C++ runtime.

## Building

Qt 6.5 or later with the MSVC kit, CMake 3.21, and Visual Studio 2022 or later.

```
cmake --preset vs -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset release
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\vs\Release\tondo.exe
```

Pushing a `v*` tag builds, signs, forges the installer with
[Forge](https://github.com/Locke-Werks/Forge), signs that, checks it, and
publishes the release.

## How the rings work

`RingTextLayout` replaces Qt's document layout. Each line is told that its width
is the arc length of its ring, so Qt's own `QTextLayout` still does the shaping
and decides where lines break. Then every glyph is picked up off that straight
line, carried round to the angle its position maps to, and turned to face the
tangent there.

`RoundEdit` is the editor around it. A `QTextDocument` holds the text and handles
undo and search. Everything involving geometry is done in radius and angle:
where a click landed, which way is up, what a selection looks like. A selection
is a curved band, since this is that kind of program.

## License

GPLv3. See [LICENSE](LICENSE).
