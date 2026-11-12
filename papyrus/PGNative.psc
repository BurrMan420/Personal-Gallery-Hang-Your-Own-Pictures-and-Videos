Scriptname PGNative Hidden
{Personal Gallery native helper (PersonalGalleryNative.dll).}

; ---- placement (moved every frame natively - no reloads, no flicker) ----
; While placing, the painting follows the crosshair (snapping to walls/floors/ceilings) or floats ahead.
; Native controls: click / E place, right-click / Backspace cancel, wheel adjusts (R: size, rotate, tilt,
; depth), F lock, Q snap / free, Z straighten, Ctrl fine, X take down, Tab gallery.
Function PlaceBegin(ObjectReference painting, Bool snap, Float scale) Global Native
Function PlaceSetRef(ObjectReference painting) Global Native
Function PlaceSetScale(Float scale) Global Native
; Stops placing; returns [x, y, z, angleX, angleY, angleZ, scale] (all 0 if nothing was being placed)
Float[] Function PlaceEnd() Global Native
; Ray from the camera; returns [found, x, y, z, angleX, angleZ, surface]
Float[] Function SnapTarget(ObjectReference painting, Float maxDist) Global Native

; ---- picture library (Data\PersonalGallery\Pictures and its subfolders) ----
Int Function GetPictureCount() Global Native ; pictures that exist
String Function GetPictureKey(Int index) Global Native
String Function GetPictureId(Int index) Global Native
String Function GetPictureName(Int index) Global Native
Int Function GetPictureAspect(Int index) Global Native
; key: path inside the Pictures folder; id: content fingerprint (finds renamed or moved files). -1 if missing.
Int Function FindPicture(String key, String id) Global Native
; index if that picture still exists, else the next one that does (-1 if there are none)
Int Function ValidPicture(Int index) Global Native
Int Function NextPicture(Int index) Global Native
; look again for added, removed and renamed pictures (done at every game start; the MCM's Scan does it mid-game)
; returns [added, removed, renamed or moved]
Int[] Function ScanPictures() Global Native

; Put a picture (with a filter and tone) on a painting; done as soon as the texture is ready.
Function ApplyPicture(ObjectReference painting, Int index, Int filter, Int tone) Global Native
; parchment saying the painting's picture file is missing
Function ShowMissing(ObjectReference painting) Global Native

; ---- magic: a glow along the picture's edge and around the frame (0 = none) ----
; style: where it sits - 0 along the edge, 1 soft aura, 2 corner gems, 3 sweeping sheen, 4 drifting veil,
; 5 grand, 6 classic (none of these pieces: PGControlScript plays Skyrim's own effect shader instead)
Function SetMagic(ObjectReference painting, Int magic, Int style) Global Native

; ---- the rest of a painting's look (native/src/Look.h Option order: glow strength, frame width, frame
; depth, mat, glass, age, lamp, nameplate, zoom, pan x, pan y, flip, volume, play once, quality,
; slideshow time, light level) and the nameplate's words ----
Function SetLook(ObjectReference painting, Int[] options, String title) Global Native
; slideshow: the picture after index (mode 1 in order, 2 shuffle, 3 favourites, 4 same album)
Int Function NextSlidePicture(Int index, Int mode) Global Native

; ---- living portraits (videos) ----
Bool Function IsVideo(Int index) Global Native
; mode: 0 still, 1 plays while looked at (pauses when you look away), 2 always moving
Function SetLive(ObjectReference painting, Int index, Int filter, Int tone, Int mode) Global Native
Function StopLive(ObjectReference painting) Global Native
; Animate Portrait: returns 1 awake, 0 asleep, -1 not a video painting
Int Function ToggleAwake(ObjectReference painting) Global Native
Function SetVideoSettings(Float volume, Int maxPlaying, Int quality, Int fps) Global Native
; picture sharpness: Standard (2048, light on video memory) or High (4096)
Function SetTextureDetail(Bool high) Global Native

; ---- build-mode gallery ----
; menu: open the gallery panel now (otherwise the placing HUD; Tab or menuKey toggles it)
; isNew: a new painting (otherwise an existing one is being adjusted)
; selection: every choice in UI.h Tab order (picture .. glow, then the 17 Look options); title: nameplate words
Function BuildModeOpen(Int[] selection, Bool menu, Int menuKey, Bool isNew, String title, String stashName) Global Native
; the other hung paintings, so the one being placed can line up with them
Function SetOtherPaintings(Form[] paintings) Global Native
Function BuildModeSet(Int tab, Int index) Global Native
Function BuildModeClose() Global Native
Bool Function IsMenuOpen() Global Native
; build mode is open on the native side (false after a game load)
Bool Function IsBuilding() Global Native
; a saved style's choices, in BuildModeOpen's selection order (the picture slot is not used)
Int[] Function GetStyle(Int index) Global Native
; the look new paintings start with, if the player saved one ("My default" in the gallery); empty if not
Int[] Function GetDefaultStyle() Global Native
; whether paintings cast shadows (every painting is refitted with it)
; a build-mode key of the player's choosing (action: PGControlScript's hotkey list; key: a scan code)
Function SetHotkey(Int slot, Int keyCode) Global Native
; a painting's size, any size (ObjectReference.SetScale stops at 10)
Function SetRefScale(ObjectReference ref, Float scale) Global Native
Function SetShadows(Bool on) Global Native
; the made looks and filtered videos on disk, in megabytes; and removing them all (each is made again when needed)
Int Function GetCacheMB() Global Native
Function ClearCache() Global Native

; ---- hidden stash and day / night ----
; swing a painting aside on its left edge (open) or back where it hangs
; style: 0 swings left, 1 swings right, 2 lifts up, 3 drops down, 4 slides left, 5 slides right, 6 slides up
Function SwingPainting(ObjectReference painting, Bool open, Int style) Global Native
; where the wall safe goes behind a painting: [x, y, z, angleX, angleY, angleZ, scale]
Float[] Function StashSpot(ObjectReference painting) Global Native
; the gallery painting the crosshair rests on within maxDist, or None (paintings carry no activation prompt)
ObjectReference Function PaintingInView(Float maxDist) Global Native
; adjusting a hung painting: start locked where it hangs (F frees it)
Function PlaceLockHere() Global Native
; the gallery key, so the plugin answers its press at once; and taking that back when the gallery won't open
Function SetMenuKey(Int key) Global Native
Function CancelOpening() Global Native
; the preview light: the plugin keeps it over your shoulder while the gallery panel is open (None: none)
Function SetPreviewLight(ObjectReference light) Global Native
; gallery mode (Home): look at a painting to change it, N for a new one, Esc to leave
Function Browse(Bool on) Global Native
Bool Function IsBrowsing() Global Native
; what Undo can reach, newest first (empty strings are skipped)
Function SetUndoList(String[] items) Global Native
; the slideshow pictures the gallery shows ticked (fingerprints)
Function SetSlidePicks(String[] ids) Global Native
; a painting's light, so the player's colour, strength and reach reach it
Function LinkLight(ObjectReference painting, ObjectReference light) Global Native
; the hidden stash's safe door swings open on its hinge, or shut
Function SafeDoor(ObjectReference safe, Bool open) Global Native
; what the player calls the hidden stash ("" = Hidden stash), for its prompt
Function SetStashName(ObjectReference painting, String name) Global Native
; night < 0: the same picture all day; otherwise the plugin shows `night` between dusk and dawn
Function SetDayNight(ObjectReference painting, Int day, Int night, Int filter, Int tone, Int life) Global Native
