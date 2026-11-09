## Route 5 at night (N0) — scouted 2026-11-07 on release game 80177ce8 (native 5279728, decomp cbae636)

Save: copy of gt4-native-game/build/r5_saves (GT mode, 0 %, 100,000,000 Cr, Nissan 240SX '96), fresh per run.
(The approved odin-saves-pe-race2 is mid Pan Euro Championship: GT4 refuses to leave GT mode, so Arcade is locked.)

Host: gt4_host --headless --session-dir DIR --no-audio --game build/game_release/gt4_game.exe --saves COPY
      --preset original --set display.frame_interpolation=off --set texture.hd_pack=off
      env GT4_ELF, GT4_ISO; GT4_HOST_RECORD=CAP,trigger:DIR/rec_go,COUNT

Commands go in ONE batch (files in DIR/commands, sorted names). Sending them one at a time through
host_session.py adds ~0.5 s each, so a "press" then lasts 30+ VBlanks and menus overshoot.

1. vwait 1400; Start (hold 6), vwait 600            -> GT mode, Home (the save loads into GT mode)
2. Triangle, vwait 1000 -> map; Triangle, vwait 500  -> "Do you really want to quit Gran Turismo Mode?" (OK)
3. Cross, vwait 1800                                 -> title attract
4. Start, vwait 200 -> title menu (Arcade Mode); Down, vwait 40; Up, vwait 40; Cross, vwait 150 -> Mode Selection
   (Cross alone on the title menu is ignored; it needs a direction press first)
5. Right, vwait 40 (Time Trial); Cross, vwait 600    -> World / Original / City / Dirt
6. Down, vwait 40 (City Courses); Cross, vwait 600   -> City Courses row (Citta di Aria selected)
7. The row's pointer glides: a one-host-frame tap (button X 1, wait 1, button X 0) moves 2-5 places. Tap
   Right/Left and check the bottom line ("Course length:1.53miles  Elevation change:48.2ft" = C.S.Route 5)
   until C.S.Route 5 is selected.
8. Cross, vwait 600 -> Normal / Reverse; Cross (Normal), vwait 600 -> Car Selection
9. Cross, vwait 300 (By Manufacturer); Cross, vwait 300 (Ford); Cross, vwait 300 (Ford Ka '01);
   Cross, vwait 600 (colour); Cross, vwait 900 (Driving Options: Automatic, TCS & ASM, Sports: Medium -> OK);
   Cross, vwait 900 (Time Trial screen, start icon) -> on the grid, Clubman Stage Route 5, night
10. Hold Cross (accelerator): the car rides the walls but moves on; in 20 s about a third of the lap.
