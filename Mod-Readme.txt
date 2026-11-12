PERSONAL GALLERY
Hang your own pictures and videos anywhere in Skyrim - as paintings, tapestries, scrying basins,
Dwemer projections and more, with lore-friendly frames, painterly filters and magic.

REQUIREMENTS
- SKSE64 (matching your game version)
- Address Library for SKSE Plugins
- PapyrusUtil SE (StorageUtil, keeps every painting's settings in your save)
- SkyUI (optional: the settings menu, MCM)
- Windows 10 or 11. Windows "N" editions need the Media Feature Pack for videos.

INSTALL
Install with your mod manager and launch through SKSE. ESL-flagged: it doesn't use a plugin slot.

ADDING YOUR PICTURES
Put pictures and videos in   Data\PersonalGallery\Pictures   (subfolders become albums).
They are picked up every time the game starts; added some while playing? MCM > Scan for new pictures.
Renaming or moving a file keeps it on its paintings. Removing one shows a "picture missing" parchment.

  Pictures: PNG, JPG/JPEG/JFIF, BMP, WebP, TIFF, TGA, DDS, GIF (animated GIFs play),
            HEIC/AVIF where Windows has those codecs
  Videos:   MP4, M4V, MOV, MKV, WebM, WMV, AVI
  Shapes:   anything from a 1:3 scroll to a 4:1 panorama; each picture uses the nearest canvas
            shape (centred).

USING IT
Home              Gallery mode: look at one of your paintings and press E (or click) to change
                  it, N to hang a new one, Ctrl+Z to undo, Esc or Home to leave. Home while
                  looking at a painting changes it straight away. A painting you change stays
                  locked where it hangs (F frees it to move), Tab opens its look, and Backspace,
                  right-click or Cancel put back exactly how and where it was.
                  Home is the default gallery key - change it in the MCM.
In the gallery    Click cards; double-click to place. Q / E change page, Enter places, Tab goes back.
Placing           Click / E place, right-click cancel, wheel adjusts, R chooses what the wheel adjusts
                  (size, spin, turn left / right, tilt, depth), F lock spot, Q snap / free,
                  T align with nearby paintings, G grid steps, Z resets the setting you are on,
                  Ctrl fine steps, X take down,
                  M mute / P pause a video while you place it
Anywhere          Ctrl+Z undo (up to 10 changes back; the gallery's Undo lists them),
                  Ctrl+C / Ctrl+V copy and paste a look
Controller        Gallery: D-pad / left stick browse, LB / RB page, A place, B back, Y favourite.
                  Placing: A place, B cancel, LT / RT adjust, X next setting, Y gallery,
                  D-pad: lock, snap, align, grid; RB resets the setting; Back undo;
                  LS / RS mute / pause a video.
Paintings show no "Activate" prompt - walking past your art stays clean. To change a hung painting,
look at it and press Home: everything (adjust, copy / paste look, take down) is in the gallery.
The gallery opens from the keyboard (Home, or the key set in the MCM); once open, a controller works
everywhere in it.

HIDDEN STASH
Finish > Hidden stash puts a wall safe, in Skyrim's own safe style, behind a painting. The painting
stands a little off the wall to cover it. Choose how the painting opens: swings left or right, lifts
up, drops down, or slides left, right or up - choosing one plays it on the painting, and Show me
plays it again. Give the safe your own name under Finish > Safe name.
Look at the painting and press Activate: the painting moves aside, the safe's door swings open, and
it shuts again when you're done. The safe sizes itself to the painting (never a toy, never a vault);
a painting too small to hide one grows just enough. Setting it back to None, or taking the painting
down, hands you everything that was inside.

DAY AND NIGHT
Picture > Day / night: choose a second picture to show from dusk (8 pm) to dawn (6 am). It switches
on its own.

PERFORMANCE
Pictures are converted once and cached (Data\textures\PersonalGallery\Cache); old cache files are
cleaned up on their own. MCM > High detail pictures uses 4096 textures (4x the video memory) -
leave it off unless you hang huge murals. For videos, the MCM has:
  Portraits moving at once (1 - 8), Video quality limit (480 - 1440) and Video frame rate (15 / 24 / 30).
  Lower them on older PCs or when many videos play in one room; sound is never affected.
Libraries of hundreds of pictures and videos are fine: only the cards on screen load, and each picture
is converted once in the background and kept in the cache.

UNINSTALLING
Empty your hidden stashes first - anything left in a safe is lost with the mod.
Hidden paintings: MCM > Show every hidden painting. Take paintings down (or Hide paintings here)
before removing the mod, then save. Your picture files are never touched.

YOUR PICTURES ARE YOURS
The mod ships no pictures. Only hang art you have the right to use, and don't upload other
people's art inside your own mods.

LICENCE AND SOURCE
Personal Gallery - Copyright (C) 2026 the Personal Gallery author.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
The full licence is in LICENSE.txt, next to this file. The complete source is in the download:
the plugin (C++) in PersonalGallery/Source, the scripts (Papyrus) in scripts/source.

CREDITS
CommonLibSSE-NG by alandtse, CharmedBaryon, Ryan McKenzie and contributors (GPL-3.0-or-later).
Dear ImGui (MIT), DirectXTex (MIT). Needs PapyrusUtil, SKSE and SkyUI.
Frames reuse Skyrim's own textures where one fits, so texture replacers apply to them too.
