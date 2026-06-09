# ArdaBest MUD Client v7 - Mudlet-style Trigger Editor

This version keeps the ARDABEST map built in and adds a Mudlet-style trigger editor.

## What changed in v7

- The **Triggers** tab now looks more like Mudlet's trigger editor.
- A default trigger named **RotS Automapper** is loaded automatically.
- The default trigger is ON by default.
- It detects room lines using the real MUD room number inside `(#31902)`.
- It does not care what the room name looks like.
- It ignores a final Mudlet mapper add-on id like `(13017)` if it appears.
- The trigger script is shown in the editor by default, but the actual map-following action is built into the C++ client.

## Room line detected

The default automapper detects lines like:

```text
Dark Tunnel Passage (#31902) [ Floor ] Exits are: E S W
```

It also detects:

```text
[HP: 316/316]>A Stone Staircase (#32681) [ Floor ] Exits are: S
Dark Tunnel Passage (#31902) [ Floor ] Exits are: E S W  (13017)
```

The final `(13017)` is ignored if it exists.

## Windows

Double-click:

```text
START-HERE-WINDOWS.bat
```

After the build finishes, run:

```text
package-windows\RUN-ARDABEST-CLIENT.bat
```

If you already have an old profile and the trigger does not show, open the Triggers tab and click:

```text
Restore RotS Automapper
```

## Linux

```bash
./build-linux.sh
./build/ardabest_client
```

## macOS

```bash
./build-macos.sh
./build/ardabest_client
```


## v11 changes

- Default connection is now `rotsmud.org` port `3791`.
- The mapper has a neon-green theme: green grid, green exit lines, green room outlines, and a bright green current-location marker.
- Right-click a room icon on the automapper to open a Mudlet-style room menu.
- Room menu options include Configure room, Set player location, Change emoji icon, Change color, Create label/details, Move to position, Move to area, Export area image, and Save map.
- Emoji choices include water, lake, trophy, food, bear, troll, orc, skull/crossbones, treasure, fire, forest, cave, mountain, shop, tavern, danger, safe room, and more.
- Type `save map` in the command line to save your custom map icons/colors/details.
- Linux users can run `START-HERE-LINUX.sh` or `start-linux.sh`.


## v11 mapping changes

- Current location pulse is now only around the icon/square, so it no longer covers nearby rooms or exit lines.
- Added Shape selector above the mapper: Square or Circle.
- Added Start Mapping / Stop Mapping buttons.
- You can also type `start mapping` and `stop mapping` in the command line.
- When Start Mapping is ON, typing `n`, `s`, `e`, `w`, `u`, or `d` records the movement. When the next room line appears, the client links the previous room to the detected room. If the room is not in ARDABEST, it creates a new room using the room line details.
- Up/down movement changes the Z layer.
- Right-click a room and choose Delete, or click a room and press Delete, to remove a mistaken room/icon and its connected exits.
- Type `save map` to save custom rooms, exits, colors, emojis, and deletions.


## v17 fixes

- The Background button opens a file picker now.
- The terminal stays black/readable instead of turning white.
- Static background images are stretched and darkened behind the text.
- The YOU ARE HERE label uses the full MUD room number when the MUD line provides it.

V25 changes:
- Adds a neon current-room line above the automapper and in the detached map window.
- The line shows: Room Name (#MUDROOM) [ Terrain ] Exits are: N E S W.
