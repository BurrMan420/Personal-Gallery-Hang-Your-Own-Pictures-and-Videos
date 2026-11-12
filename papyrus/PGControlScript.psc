Scriptname PGControlScript extends Quest
{Personal Gallery: build mode. The native gallery (PersonalGalleryNative.dll) handles browsing and
sends each choice as a PG_Select mod event; this script places, swaps and remembers paintings.}

; ---- filled by the plugin ---------------------------------------------------------------
FormList Property TypeLists Auto  ; one FormList per display/frame type (see TypeIndex), indexed by canvas shape
FormList Property LightList Auto  ; lights: each colour at dim, normal, bright (see LightIndex)
FormList Property MagicList Auto  ; Skyrim.esm effect shaders: the Animate Portrait shimmer and the Classic glow
Spell Property AnimateSpell Auto
Message Property MainMenu Auto
Message Property PaintingMenu Auto ; Adjust it / Copy its look / Paste the copied look / Take it down / Leave it
Message Property HelpMessage Auto
Message Property ConfirmTakeDown Auto
Form Property StashBase Auto ; the hidden stash's container

; ---- settings (MCM) ----------------------------------------------------------------------
Int Property MenuKey = 199 Auto ; Home
Float Property MoveSpeed = 1.0 Auto
Float Property SlideshowInterval = 10.0 Auto ; older versions; slideshows now have their own timing
Bool Property AlwaysShowHelp = False Auto
Bool Property SnapByDefault = True Auto
Float Property VideoVolume = 0.8 Auto
Int Property MaxPlaying = 2 Auto
Int Property VideoQualityLimit = 2 Auto ; no video above this: 0 Low (480), 1 Medium (720), 2 High (960), 3 Ultra (1440)
Int Property VideoFps = 2 Auto ; videos shown at 0: 15, 1: 24, 2: 30 frames a second
Float Property EyeHeight = 110.0 Auto
Bool Property HighDetail = False Auto ; pictures at 4096 instead of 2048 (more video memory)
; ---- build-mode keys (mod menu): place, cancel, gallery panel, wheel's next setting, lock, snap, align, grid, reset,
; take down, mute, pause, new painting - in the plugin's order (UI.cpp Action)
Int[] hotkeys
Int Property NumHotkeys = 13 AutoReadOnly

Int Function HotkeyDefault(Int slot)
	If slot == 0
		Return 18 ; E
	ElseIf slot == 1
		Return 14 ; Backspace
	ElseIf slot == 2
		Return 15 ; Tab
	ElseIf slot == 3
		Return 19 ; R
	ElseIf slot == 4
		Return 33 ; F
	ElseIf slot == 5
		Return 16 ; Q
	ElseIf slot == 6
		Return 20 ; T
	ElseIf slot == 7
		Return 34 ; G
	ElseIf slot == 8
		Return 44 ; Z
	ElseIf slot == 9
		Return 45 ; X
	ElseIf slot == 10
		Return 50 ; M
	ElseIf slot == 11
		Return 25 ; P
	EndIf
	Return 49 ; N
EndFunction

Int Function GetHotkey(Int slot)
	If hotkeys && hotkeys.Length == NumHotkeys && hotkeys[slot] > 0
		Return hotkeys[slot]
	EndIf
	Return HotkeyDefault(slot)
EndFunction

Function SetHotkey(Int slot, Int keyCode)
	If !hotkeys || hotkeys.Length != NumHotkeys
		hotkeys = new Int[13]
	EndIf
	hotkeys[slot] = keyCode
	PGNative.SetHotkey(slot, keyCode)
EndFunction

Bool Property NoShadows = False Auto ; paintings cast no shadows (lighter, and no odd shadows from big ones)

; catalogue sizes - must match native/src/Catalog.h and tools/catalog.py
Int Property NumFilters = 18 AutoReadOnly
Int Property NumFrames = 22 AutoReadOnly
Int Property NumSimpleDisplays = 16 AutoReadOnly
Int Property AutoFrame = 22 AutoReadOnly ; Frame tab "Auto": picks a frame to suit the magic
Int Property MatchMagicLight = 5 AutoReadOnly
Int Property NumOpts = 75 AutoReadOnly ; native/src/Look.h Option

; option indices (Look.h)
Int Property OptIntensity = 0 AutoReadOnly
Int Property OptLamp = 6 AutoReadOnly
Int Property OptSlideTime = 15 AutoReadOnly
Int Property OptLightLevel = 16 AutoReadOnly
Int Property OptStashOpen = 17 AutoReadOnly

; ---- per-painting records (StorageUtil) ----------------------------------------------------
String Property ListKey = "PG_Placed" AutoReadOnly
String Property PicKey = "PG_Pic" AutoReadOnly
String Property PicIdKey = "PG_PicId" AutoReadOnly ; content fingerprint: finds the picture again after a rename or move
String Property FilterKey = "PG_Filter" AutoReadOnly
String Property ToneKey = "PG_Tone" AutoReadOnly
String Property LifeKey = "PG_Life" AutoReadOnly
String Property MagicKey = "PG_Magic" AutoReadOnly
String Property GlowKey = "PG_Glow" AutoReadOnly ; where the magic sits (Catalog.h kGlowStyles)
Int Property ClassicGlow = 6 AutoReadOnly ; glow style played with Skyrim's own effect shader
String Property DisplayKey = "PG_Display" AutoReadOnly
String Property FrameKey = "PG_Frame" AutoReadOnly
String Property SizeKey = "PG_Size" AutoReadOnly
String Property LightKey = "PG_Light" AutoReadOnly
String Property LightRefKey = "PG_LightRef" AutoReadOnly
String Property SlideKey = "PG_Slideshow" AutoReadOnly ; 0 off, 1 in order, 2 shuffle, 3 favourites, 4 this album
String Property TitleKey = "PG_Title" AutoReadOnly ; nameplate words
String Property StashNameKey = "PG_StashName" AutoReadOnly ; what the player calls the hidden stash
String Property SlidePickKey = "PG_SlidePicks" AutoReadOnly ; a slideshow's own pictures (fingerprints)
String Property EditPicksKey = "PG_EditPicks" AutoReadOnly  ; the picks being edited (on this quest)
String Property StashKey = "PG_Stash" AutoReadOnly ; the container behind the painting
String Property NightKey = "PG_Night" AutoReadOnly ; 1: another picture at night
String Property NightPicKey = "PG_NightPic" AutoReadOnly
String Property NightPicIdKey = "PG_NightPicId" AutoReadOnly
String Property OptPrefix = "PG_O" AutoReadOnly ; PG_O0 .. PG_O16: the Look options
String Property UndoPrefix = "PGU_" AutoReadOnly ; a painting's record before its last change (undo)

; ---- edit state ------------------------------------------------------------------------------
Bool editing = False
Bool editIsNew = False
Bool helpShown = False
ObjectReference editRef
ObjectReference originalRef

; current choices, in gallery order: picture, filter, tone, life, magic, display, frame, size, light, slideshow, glow
Int curPic = 0
Int curFilter = 0
Int curTone = 0 ; True colours: the picture as it is (the grades are a choice, never the default)
Int curLife = 1 ; videos play while you look at them
Int curMagic = 0
Int curGlow = 0 ; along the edge
Int shownMagic = 0
Int curDisplay = 0
Int curFrame = 0
Int curSize = 1
Int curLight = 0
Int curSlide = 0
Int[] curOpt ; the Look options (Look.h order)
Int curDayNight = 0
Int curNightPic = 0
ObjectReference openStash ; the painting swung open, until the container closes
ObjectReference openBox   ; its safe
Int openStyle = 0
Bool stashMenuSeen = False ; its container menu came up
String curTitle = ""
String curStashName = ""

Float scale = 1.0

Float origX
Float origY
Float origZ
Float origAX
Float origAY
Float origAZ
Float origScale

; ---- undo: the last placement, change or take-down -------------------------------------------
Int undoKind = 0 ; 0 nothing, 1 placed a new painting, 2 changed one, 3 took one down
ObjectReference undoRef ; the painting now (kind 1, 2) or the one taken down (kind 3)
ObjectReference undoOld ; kind 2 when the change swapped the record: the old painting, kept hidden
; the changes before it, oldest first: Undo steps back through them (10 deep; the oldest is then made final)
Int[] hKind
ObjectReference[] hRef
ObjectReference[] hOld
Int[] hTag ; a change made in place keeps its earlier record under its own key (UndoPrefix + tag + "_")
Float[] hPos ; 7 per change: x, y, z, angle x, y, z, scale
Int hCount = 0
Int nextUndoTag = 0
Float uX
Float uY
Float uZ
Float uAX
Float uAY
Float uAZ
Float uScale

; ---- copied look -------------------------------------------------------------------------------
Bool hasCopy = False
Int cFilter
Int cTone
Int cLife
Int cMagic
Int cGlow
Int cDisplay
Int cFrame
Int cSize
Int cLight
Int cSlide
Int[] cOpt

Event OnInit()
	RegisterForKey(MenuKey)
	RegisterForModEvent("PG_Select", "OnPGSelect")
	RegisterForModEvent("PG_Stash", "OnPGStash")
	RegisterForModEvent("PG_Browse", "OnPGBrowse")
	PushSettings()
EndEvent

; The native plugin forgets settings when the game reloads; the MCM calls this then and on change.
Function PushSettings()
	MigrateVolume()
	PGNative.SetVideoSettings(VideoVolume, MaxPlaying, VideoQualityLimit, VideoFps)
	PGNative.SetMenuKey(MenuKey)
	PGNative.SetTextureDetail(False) ; (the 4096 setting is gone: four times the memory for no visible gain)
	If AnimateSpell && Game.GetPlayer().HasSpell(AnimateSpell)
		Game.GetPlayer().RemoveSpell(AnimateSpell) ; the spell is gone: the Video page's "Moves" does its job
	EndIf
	PGNative.SetShadows(!NoShadows)
	Int hk = 0
	While hk < NumHotkeys
		PGNative.SetHotkey(hk, GetHotkey(hk))
		hk += 1
	EndWhile
	RegisterForKey(MenuKey)
	RegisterForModEvent("PG_Select", "OnPGSelect")
	RegisterForModEvent("PG_Stash", "OnPGStash")
	RegisterForModEvent("PG_Browse", "OnPGBrowse")
	RecoverEdit()
EndFunction

; A save has loaded. The paintings already in sight get no OnLoad of their own then, so they stood blank until
; each was edited or refreshed by hand: put every loaded one's picture and look back on. (Through an event to
; ourselves, so the mod menu that tells us is not kept waiting.)
Function GameReloaded()
	RegisterForModEvent("PG_Reload", "OnPGReload")
	SendModEvent("PG_Reload")
EndFunction

Event OnPGReload(String eventName, String what, Float value, Form sender)
	Int pass = 0
	While pass < 2 ; again a little later, for any whose 3D was not there yet
		If pass == 0
			Utility.Wait(1.0)
		Else
			Utility.Wait(4.0)
		EndIf
		Form[] all = StorageUtil.FormListToArray(None, ListKey)
		Int i = 0
		While i < all.Length
			ObjectReference ref = all[i] as ObjectReference
			If ref && ref.IsEnabled() && ref.Is3DLoaded() && !(editing && ref == editRef)
				Reapply(ref)
				If pass == 0 && StorageUtil.GetIntValue(ref, SlideKey) > 0
					(ref as PGPaintingScript).StartSlideshow()
				EndIf
			EndIf
			i += 1
		EndWhile
		pass += 1
	EndWhile
EndEvent

; Volume used to be five steps (Mute .. Very loud); it is now 0 - 200 % in 5 % steps. Once per save.
Function MigrateVolume()
	If StorageUtil.GetIntValue(None, "PG_VolumePercent") == 1
		Return
	EndIf
	Int[] map = new Int[5]
	map[0] = 0
	map[1] = 8
	map[2] = 20
	map[3] = 34
	map[4] = 40
	Form[] placed = StorageUtil.FormListToArray(None, ListKey)
	Int i = 0
	While i < placed.Length
		If placed[i] && StorageUtil.HasIntValue(placed[i], OptPrefix + 12)
			Int v = StorageUtil.GetIntValue(placed[i], OptPrefix + 12)
			If v >= 0 && v <= 4
				StorageUtil.SetIntValue(placed[i], OptPrefix + 12, map[v])
			EndIf
		EndIf
		i += 1
	EndWhile
	If curOpt && curOpt.Length > 12 && curOpt[12] >= 0 && curOpt[12] <= 4
		curOpt[12] = map[curOpt[12]]
	EndIf
	StorageUtil.SetIntValue(None, "PG_VolumePercent", 1)
EndFunction

; A save made while building loads with build mode gone from the native side: put things back.
Function RecoverEdit()
	If editing && !PGNative.IsBuilding()
		CancelEdit()
	EndIf
EndFunction

Bool starting = False ; Home pressed: opening the gallery (more presses wait)
Float startedAt = 0.0  ; when (an opening that never finished stops blocking after a few seconds)

Function SetMenuKey(Int newKey)
	UnregisterForKey(MenuKey)
	MenuKey = newKey
	RegisterForKey(MenuKey)
	PGNative.SetMenuKey(MenuKey)
EndFunction

Bool Function HasNative()
	Return SKSE.GetPluginVersion("PersonalGalleryNative") > 0
EndFunction

; =============================================================================================
; Look options (crop, video, frame shape, finish, glow strength, slideshow timing, light level)
; =============================================================================================

Int Function OptDefault(Int i)
	If i == 27
		Return 0 ; corner gems: as the glow style
	ElseIf i == 0 || i == 1 || i == 2 || i == 15 || i == 16
		Return 1
	ElseIf i == 9 || i == 10
		Return 4 ; centred
	ElseIf i == 12
		Return 20 ; volume 100 % (0 - 40 in 5 % steps)
	ElseIf i == 19
		Return 16 ; magic saturation 80 %
	ElseIf i == 20 || i == 23 || (i >= 25 && i <= 29) || i == 32 || i == 33
		Return 10 ; 100 %: magic strength, speed, where it shines; light strength and reach
	ElseIf i == 21
		Return 20 ; magic opacity 100 %
	ElseIf i >= 34 && i <= 38
		Return 15 ; picture brightness, contrast, gamma, saturation, warmth: untouched
	ElseIf i == 56
		Return 20 ; filterstrength
	ElseIf i == 57
		Return 10 ; stashamount
	ElseIf i == 74
		Return 10 ; projturn: straight
	ElseIf i == 58
		Return 6 ; stashspeed: 0.8 seconds
	ElseIf i == 59
		Return 10 ; glassshine
	ElseIf i == 60
		Return 1 ; matwidth: normal
	ElseIf i == 61
		Return 0 ; platefont: classic
	ElseIf i == 62
		Return 6 ; beam: 60%
	ElseIf i == 63
		Return 5 ; vision: 50%
	ElseIf i == 64
		Return 1 ; glassthick: normal
	ElseIf i == 65
		Return 0 ; framehue: its own
	ElseIf i == 66
		Return 10 ; frametint: 50%
	ElseIf i == 67
		Return 0 ; platehue: its own
	ElseIf i == 68
		Return 1 ; platesize: normal
	ElseIf i == 69
		Return 10 ; projtilt: level
	ElseIf i == 70
		Return 5 ; projsize: 100 %
	ElseIf i == 72
		Return 10 ; mattint: 50 %
	ElseIf i == 39
		Return 3 ; picgloss
	ElseIf i == 40
		Return 20 ; picopacity
	ElseIf i == 42
		Return 8 ; tintstrength
	ElseIf i == 44
		Return 10 ; sharpness
	ElseIf i == 45
		Return 10 ; sheenspeed
	ElseIf i == 46
		Return 10 ; lightside
	ElseIf i == 47
		Return 10 ; lightheight
	ElseIf i == 48
		Return 10 ; lightout
	ElseIf i == 50
		Return 10 ; lightflickerspeed
	ElseIf i == 53
		Return 20 ; nightstart
	ElseIf i == 54
		Return 6 ; nightend
	ElseIf i == 14
		Return 2 ; high quality
	EndIf
	Return 0
EndFunction

; gallery event key -> option index, -1 for anything else
Int Function OptIndex(String tab)
	If tab == "intensity"
		Return 0
	ElseIf tab == "framewidth"
		Return 1
	ElseIf tab == "framedepth"
		Return 2
	ElseIf tab == "mat"
		Return 3
	ElseIf tab == "glass"
		Return 4
	ElseIf tab == "age"
		Return 5
	ElseIf tab == "lamp"
		Return 6
	ElseIf tab == "plate"
		Return 7
	ElseIf tab == "zoom"
		Return 8
	ElseIf tab == "panx"
		Return 9
	ElseIf tab == "pany"
		Return 10
	ElseIf tab == "flip"
		Return 11
	ElseIf tab == "volume"
		Return 12
	ElseIf tab == "play"
		Return 13
	ElseIf tab == "quality"
		Return 14
	ElseIf tab == "slidetime"
		Return 15
	ElseIf tab == "lightlevel"
		Return 16
	ElseIf tab == "stashopen"
		Return 17
	ElseIf tab == "magichue"
		Return 18
	ElseIf tab == "magicsat"
		Return 19
	ElseIf tab == "magicstrength"
		Return 20
	ElseIf tab == "magicalpha"
		Return 21
	ElseIf tab == "motion"
		Return 22
	ElseIf tab == "speed"
		Return 23
	ElseIf tab == "depth"
		Return 24
	ElseIf tab == "mixedge"
		Return 25
	ElseIf tab == "mixaura"
		Return 26
	ElseIf tab == "mixcorners"
		Return 27
	ElseIf tab == "mixsheen"
		Return 28
	ElseIf tab == "mixveil"
		Return 29
	ElseIf tab == "sweep"
		Return 30
	ElseIf tab == "lighthue"
		Return 31
	ElseIf tab == "lightbright"
		Return 32
	ElseIf tab == "lightreach"
		Return 33
	ElseIf tab == "picbright"
		Return 34
	ElseIf tab == "piccontrast"
		Return 35
	ElseIf tab == "picgamma"
		Return 36
	ElseIf tab == "picsat"
		Return 37
	ElseIf tab == "picwarm"
		Return 38
	ElseIf tab == "picgloss"
		Return 39
	ElseIf tab == "picopacity"
		Return 40
	ElseIf tab == "tinthue"
		Return 41
	ElseIf tab == "tintstrength"
		Return 42
	ElseIf tab == "vignette"
		Return 43
	ElseIf tab == "sharpness"
		Return 44
	ElseIf tab == "sheenspeed"
		Return 45
	ElseIf tab == "lightside"
		Return 46
	ElseIf tab == "lightheight"
		Return 47
	ElseIf tab == "lightout"
		Return 48
	ElseIf tab == "lightflicker"
		Return 49
	ElseIf tab == "lightflickerspeed"
		Return 50
	ElseIf tab == "lightwhen"
		Return 51
	ElseIf tab == "magicwhen"
		Return 52
	ElseIf tab == "nightstart"
		Return 53
	ElseIf tab == "nightend"
		Return 54
	ElseIf tab == "sheenangle"
		Return 55
	ElseIf tab == "filterstrength"
		Return 56
	ElseIf tab == "stashamount"
		Return 57
	ElseIf tab == "stashspeed"
		Return 58
	ElseIf tab == "glassshine"
		Return 59
	ElseIf tab == "matwidth"
		Return 60
	ElseIf tab == "platefont"
		Return 61
	ElseIf tab == "beam"
		Return 62
	ElseIf tab == "vision"
		Return 63
	ElseIf tab == "glassthick"
		Return 64
	ElseIf tab == "framehue"
		Return 65
	ElseIf tab == "frametint"
		Return 66
	ElseIf tab == "platehue"
		Return 67
	ElseIf tab == "platesize"
		Return 68
	ElseIf tab == "projtilt"
		Return 69
	ElseIf tab == "projsize"
		Return 70
	ElseIf tab == "mathue"
		Return 71
	ElseIf tab == "mattint"
		Return 72
	ElseIf tab == "videofps"
		Return 73
	ElseIf tab == "projturn"
		Return 74
	EndIf
	Return -1
EndFunction

; The tone number the plugin takes: the grade (0..15) + 16 x the five picture adjustments, 5 bits each
; (0 untouched, else the slider + 1; sliders 0..30 with 15 in the middle).
Int Function ToneCode(Int grade)
	EnsureOpts()
	Int code = 0
	Int unit = 1
	Int i = 34
	While i <= 38
		Int v = curOpt[i]
		If v != 15 && i != 36 && i != 38 ; (gamma and warmth are no longer offered: they stay untouched)
			code += (v + 1) * unit
		EndIf
		unit *= 32
		i += 1
	EndWhile
	Return grade + code * 16
EndFunction

; A painting's option: the one being edited from what is chosen now, any other from its record.
Int Function OptOf(ObjectReference painting, Int i)
	If painting == editRef && curOpt && curOpt.Length > i
		Return curOpt[i]
	EndIf
	Return StorageUtil.GetIntValue(painting, OptPrefix + i, OptDefault(i))
EndFunction

Int[] Function DefaultOpts()
	Int[] o = new Int[75]
	Int i = 0
	While i < NumOpts
		o[i] = OptDefault(i)
		i += 1
	EndWhile
	Return o
EndFunction

Function EnsureOpts()
	If !curOpt || curOpt.Length != NumOpts ; saves from before these options existed: keep what was chosen, defaults for the rest
		Int[] o = DefaultOpts()
		Int i = 0
		While curOpt && i < curOpt.Length && i < NumOpts
			o[i] = curOpt[i]
			i += 1
		EndWhile
		curOpt = o
	EndIf
EndFunction

; A copied look's option - one copied before an update has fewer: the newer ones take their defaults
; (never 0, which would make a painting see-through or its light and magic dark).
Int Function CopiedOpt(Int i)
	If cOpt && i < cOpt.Length
		Return cOpt[i]
	EndIf
	Return OptDefault(i)
EndFunction

Int[] Function StoredOpts(ObjectReference ref)
	Int[] o = new Int[75]
	Int i = 0
	While i < NumOpts
		o[i] = StorageUtil.GetIntValue(ref, OptPrefix + i, OptDefault(i))
		i += 1
	EndWhile
	Return o
EndFunction

Function ApplyLook(ObjectReference ref)
	EnsureOpts()
	PGNative.SetLook(ref, curOpt, curTitle)
EndFunction

; =============================================================================================
; Input
; =============================================================================================

; Home: look at one of your paintings to move it, anywhere else to hang a new one.
; While building, the native plugin handles every key and sends the result as PG_Select.
Event OnKeyDown(Int keyCode)
	If keyCode != MenuKey
		Return
	EndIf
	RecoverEdit() ; an edit the plugin no longer shows (a save loaded mid-edit): let it go first
	If starting && Utility.GetCurrentRealTime() - startedAt < 8.0
		Return ; still opening: the extra presses wait for it (no second painting)
	EndIf
	If editing
		Return
	EndIf
	If PGNative.IsBrowsing()
		PGNative.Browse(False)
		Return
	EndIf
	If Utility.IsInMenuMode()
		PGNative.CancelOpening()
		Return
	EndIf
	If !HasNative()
		Debug.MessageBox("Personal Gallery needs PersonalGalleryNative.dll (SKSE plugin). Reinstall the mod and launch through SKSE.")
		Return
	EndIf
	starting = True
	startedAt = Utility.GetCurrentRealTime()
	ObjectReference aimed = PGNative.PaintingInView(600.0) ; paintings carry no activation prompt
	If aimed && (aimed as PGPaintingScript)
		StartAdjust(aimed)
	Else
		PGNative.Browse(True) ; gallery mode: change what you look at, N for a new painting
	EndIf
	starting = False
EndEvent

; Gallery mode: the plugin sends the painting to change ("edit"), or asks for a new one ("new").
Event OnPGBrowse(String eventName, String what, Float value, Form sender)
	If editing || (starting && Utility.GetCurrentRealTime() - startedAt < 8.0) || Utility.IsInMenuMode()
		Return
	EndIf
	starting = True
	startedAt = Utility.GetCurrentRealTime()
	ObjectReference ref = sender as ObjectReference
	If what == "edit" && ref && (ref as PGPaintingScript)
		StartAdjust(ref)
	ElseIf what == "new"
		StartNew()
	EndIf
	starting = False
EndEvent

; E (or A) on a painting with a hidden stash: the plugin's prompt sends the painting.
Event OnPGStash(String eventName, String strArg, Float value, Form sender)
	ObjectReference painting = sender as ObjectReference
	If painting && !editing && !openStash
		OpenStash(painting)
	EndIf
EndEvent

; A choice from the native gallery: strArg = choice key or action, numArg = index.
Event OnPGSelect(String eventName, String tab, Float value, Form sender)
	If tab == "undo"
		If editing
			CancelEdit()
		EndIf
		Undo()
		Return
	EndIf
	If tab == "undoto" ; the gallery's undo list: back to (and including) that change
		If editing
			CancelEdit()
		EndIf
		Int steps = (value as Int) + 1
		While steps > 0
			Undo()
			steps -= 1
		EndWhile
		Return
	EndIf
	If !editing
		Return
	EndIf
	EnsureOpts()
	Int i = value as Int
	Int opt = OptIndex(tab)
	If opt >= 34 && opt <= 38 ; the picture's own colour: a new texture
		curOpt[opt] = i
		curTone = ToneCode(curTone % 16)
		PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
		PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
		ApplyDayNight(editRef)
	ElseIf opt >= 0
		curOpt[opt] = i
		ApplyLook(editRef)
		If opt == OptLightLevel || opt == OptLamp
			SetLight(editRef) ; dim / normal / bright, and the light a lamp brings: at once
		EndIf
		If opt >= 46 && opt <= 48 ; the light moved: put it there
			ObjectReference lightRef = StorageUtil.GetFormValue(editRef, LightRefKey) as ObjectReference
			If lightRef
				PositionLight(editRef, lightRef)
			EndIf
		EndIf
	ElseIf StringUtil.Find(tab, "platetext|") == 0
		curTitle = StringUtil.Substring(tab, 10)
		ApplyLook(editRef)
	ElseIf StringUtil.Find(tab, "slidepick|") == 0 ; tick or untick a picture for this slideshow
		String pickId = StringUtil.Substring(tab, 10)
		If StorageUtil.StringListHas(self, EditPicksKey, pickId)
			StorageUtil.StringListRemove(self, EditPicksKey, pickId, True)
		Else
			StorageUtil.StringListAdd(self, EditPicksKey, pickId, False)
		EndIf
	ElseIf StringUtil.Find(tab, "stashname|") == 0
		curStashName = StringUtil.Substring(tab, 10)
		PGNative.SetStashName(editRef, curStashName)
	ElseIf tab == "place"
		FinishEdit()
	ElseIf tab == "cancel"
		CancelEdit()
	ElseIf tab == "copy"
		CopyCurrent()
	ElseIf tab == "paste"
		PasteCurrent()
	ElseIf tab == "style"
		Int[] v = PGNative.GetStyle(i)
		If v.Length >= 29
			ApplyValues(v)
			Debug.Notification("Style applied")
		EndIf
	ElseIf tab == "takedown"
		TakeDownEdited()
	ElseIf tab == "nearest"
		CancelEdit()
		ObjectReference nearest = FindNearest()
		If nearest
			StartAdjust(nearest)
		EndIf
	ElseIf tab == "hide" || tab == "show"
		CancelEdit()
		SetAreaHidden(tab == "hide")
	ElseIf tab == "daynight"
		curDayNight = i
		If i == 1 && PGNative.ValidPicture(curNightPic) < 0
			curNightPic = curPic
		EndIf
		ApplyDayNight(editRef)
	ElseIf tab == "nightpic"
		curNightPic = i
		ApplyDayNight(editRef)
	ElseIf tab == "picture"
		curPic = i
		UpdateBase()
		ApplyDayNight(editRef)
	ElseIf tab == "defaults" ; every setting back to default (the picture, display and frame stay)
		curOpt = DefaultOpts()
		curFilter = 0
		curTone = ToneCode(0)
		curMagic = 0
		curGlow = 0
		curLight = 0
		curSlide = 0
		curLife = 1
		curDayNight = 0 ; (the gallery shows all of these as reset: they must be)
		PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
		PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
		ApplyLook(editRef)
		ShowMagic(editRef, curMagic)
		SetLight(editRef) ; the light tried goes too
		ApplyDayNight(editRef)
	ElseIf tab == "filter"
		curFilter = i
		PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
		PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
		ApplyDayNight(editRef)
	ElseIf tab == "tone"
		curTone = ToneCode(i)
		PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
		PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
		ApplyDayNight(editRef)
	ElseIf tab == "life"
		curLife = i
		PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
	ElseIf tab == "magic"
		curMagic = i
		If curFrame == AutoFrame && (curDisplay == 0 || curDisplay == 7)
			UpdateBase() ; Auto frame follows the magic
		EndIf
		ShowMagic(editRef, curMagic)
		If curLight == MatchMagicLight
			SetLight(editRef) ; a light that matches the magic changes colour with it
		EndIf
	ElseIf tab == "glow"
		curGlow = i
		ShowMagic(editRef, curMagic)
	ElseIf tab == "display"
		curDisplay = i
		UpdateBase()
	ElseIf tab == "frame"
		curFrame = i
		If curDisplay == 0 || curDisplay == 7
			UpdateBase()
		EndIf
	ElseIf tab == "size"
		curSize = i
		scale = SizeScale(i)
		PGNative.PlaceSetScale(scale)
	ElseIf tab == "light"
		curLight = i
		SetLight(editRef) ; at once: you see it while you place
	ElseIf tab == "slideshow"
		curSlide = i
	EndIf
EndEvent

Function ShowMainMenu()
	If !HasNative()
		Debug.MessageBox("Personal Gallery needs PersonalGalleryNative.dll (SKSE plugin). Reinstall the mod and launch through SKSE.")
		Return
	EndIf
	Int choice = MainMenu.Show()
	If choice == 0
		StartNew()
	ElseIf choice == 1
		ObjectReference ref = FindNearest()
		If ref
			StartAdjust(ref)
		EndIf
	ElseIf choice == 2
		ObjectReference ref2 = FindNearest()
		If ref2
			TakeDown(ref2)
		EndIf
	ElseIf choice == 3
		SetAreaHidden(True)
	ElseIf choice == 4
		SetAreaHidden(False)
	EndIf
EndFunction

; Called by PGPaintingScript when the player activates a painting.
Function PaintingActivated(ObjectReference ref)
	If editing
		If ref == editRef
			FinishEdit()
		EndIf
		Return
	EndIf
	Int choice = PaintingMenu.Show()
	If choice == 0
		StartAdjust(ref)
	ElseIf choice == 1
		OpenStash(ref)
	ElseIf choice == 2
		CopyFrom(ref)
	ElseIf choice == 3
		PasteOnto(ref)
	ElseIf choice == 4
		TakeDown(ref)
	EndIf
EndFunction

; =============================================================================================
; Placing and adjusting
; =============================================================================================

Function StartNew()
	If PGNative.GetPictureCount() == 0
		PGNative.CancelOpening()
		Debug.MessageBox("No pictures yet.\n\nPut your images or videos in Data\\PersonalGallery\\Pictures, then use Scan for new pictures in the mod menu (MCM) - or restart the game.")
		Return
	EndIf
	EnsureOpts()
	; A new painting starts from the player's saved default look ("My default" in the gallery) or, without one, from
	; the built-in look with the display and frame last used. (It used to carry the whole last look over, so one
	; painting's brightness, tint or magic turned up on the next.)
	Int newLight = 0
	Int[] mine = PGNative.GetDefaultStyle()
	If mine && mine.Length >= 11 + NumOpts
		curFilter = mine[1]
		curLife = mine[3]
		curMagic = mine[4]
		curDisplay = mine[5]
		curFrame = mine[6]
		newLight = mine[8]
		curGlow = mine[10]
		Int oi = 0
		While oi < NumOpts
			curOpt[oi] = mine[11 + oi]
			oi += 1
		EndWhile
		curTone = ToneCode(mine[2])
	Else
		curOpt = DefaultOpts()
		curFilter = 0
		curTone = 0
		curLife = 1
		curMagic = 0
		curGlow = 0
	EndIf
	curDayNight = 0
	curPic = PGNative.ValidPicture(curPic)
	editRef = Game.GetPlayer().PlaceAtMe(CurrentBase(), 1, True) ; persistent: never cleaned up by cell resets
	originalRef = None
	editIsNew = True
	curLight = newLight
	curSlide = 0
	curSize = 1
	curTitle = "" ; a new painting starts without words
	curStashName = ""
	scale = 1.0
	BeginEdit() ; the gallery opens now; the picture, look and magic come in while you look at it
	PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
	PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
	ApplyLook(editRef)
	shownMagic = 0
	ShowMagic(editRef, curMagic)
	SetLight(editRef) ; a lamp carried over from the last look brings its light at once, not only once placed
EndFunction

Function StartAdjust(ObjectReference ref)
	editRef = ref
	originalRef = ref
	editIsNew = False
	origX = ref.GetPositionX()
	origY = ref.GetPositionY()
	origZ = ref.GetPositionZ()
	origAX = ref.GetAngleX()
	origAY = ref.GetAngleY()
	origAZ = ref.GetAngleZ()
	origScale = ref.GetScale()
	Int found = FindPictureOf(ref)
	If found >= 0
		curPic = found
	EndIf
	curFilter = StorageUtil.GetIntValue(ref, FilterKey)
	curTone = StorageUtil.GetIntValue(ref, ToneKey)
	curLife = StorageUtil.GetIntValue(ref, LifeKey, 1)
	curMagic = StorageUtil.GetIntValue(ref, MagicKey)
	curGlow = StorageUtil.GetIntValue(ref, GlowKey)
	shownMagic = curMagic
	curDisplay = StorageUtil.GetIntValue(ref, DisplayKey)
	curFrame = StorageUtil.GetIntValue(ref, FrameKey)
	curSize = StorageUtil.GetIntValue(ref, SizeKey, 1)
	curLight = StorageUtil.GetIntValue(ref, LightKey)
	curSlide = StorageUtil.GetIntValue(ref, SlideKey)
	curOpt = StoredOpts(ref)
	curTitle = StorageUtil.GetStringValue(ref, TitleKey)
	curStashName = StorageUtil.GetStringValue(ref, StashNameKey)
	CopyPicks(ref, SlidePickKey, self, EditPicksKey) ; its slideshow's picks, to edit
	curDayNight = StorageUtil.GetIntValue(ref, NightKey)
	curNightPic = FindNightPictureOf(ref)
	If curNightPic < 0
		curNightPic = curPic
	EndIf
	; its light stays on and follows it (the plugin moves it with the painting): what you tune on the Light page
	; is what you see. (It used to be switched off here, so colour, strength and reach showed nothing.)
	ObjectReference lightRef = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
	If lightRef
		PGNative.LinkLight(ref, lightRef)
	EndIf
	scale = origScale
	BeginEdit()
EndFunction

Function BeginEdit()
	editing = True
	EnsureOpts()
	PGNative.SetOtherPaintings(StorageUtil.FormListToArray(None, ListKey)) ; to line up with
	PGNative.PlaceBegin(editRef, SnapByDefault, scale)
	If !editIsNew
		PGNative.PlaceLockHere() ; it stays put until you free it (F); Cancel puts back its look and spot
	EndIf
	If !previewLight && LightList ; a soft light over your shoulder while the gallery is open (true colours anywhere)
		previewLight = Game.GetPlayer().PlaceAtMe(LightList.GetAt(LightList.GetSize() - 1), 1, False, False)
	EndIf
	PGNative.SetPreviewLight(previewLight)
	PGNative.SetSlidePicks(StorageUtil.StringListToArray(self, EditPicksKey))
	SyncUndoList()
	PGNative.BuildModeOpen(Selection(), True, MenuKey, editIsNew, curTitle, curStashName) ; straight into the gallery panel, new or hung
	If AlwaysShowHelp && !helpShown ; the on-screen hints cover everything; the full help is optional (MCM)
		helpShown = True
		HelpMessage.Show()
	EndIf
EndFunction

; every choice, in the gallery's order (UI.h Tab)
Int[] Function Selection()
	Int[] sel = new Int[88]
	sel[0] = curPic
	sel[1] = curFilter
	sel[2] = curTone % 16 ; the grade; the player's adjustments ride above it (ToneCode)
	sel[3] = curLife
	sel[4] = curMagic
	sel[5] = curDisplay
	sel[6] = curFrame
	sel[7] = curSize
	sel[8] = curLight
	sel[9] = curSlide
	sel[10] = curGlow
	Int i = 0
	While i < NumOpts
		sel[11 + i] = curOpt[i]
		i += 1
	EndWhile
	sel[86] = curDayNight
	sel[87] = curNightPic
	Return sel
EndFunction

; Stops placing and writes the final position once (the only SetPosition while editing).
ObjectReference previewLight ; lights the painting you edit (gone when you are done)

Function EndEdit(Bool keep)
	editing = False
	PGNative.BuildModeClose()
	PGNative.SetPreviewLight(None)
	If previewLight
		previewLight.Disable()
		previewLight.Delete()
		previewLight = None
	EndIf
	Float[] t = PGNative.PlaceEnd()
	If keep && t && t[6] > 0.0
		scale = t[6]
		SetSize(editRef, scale)
		editRef.SetAngle(t[3], t[4], t[5])
		editRef.SetPosition(t[0], t[1], t[2])
	EndIf
EndFunction

Function FinishEdit()
	; undo: remember what this replaces
	ClearUndo()
	undoRef = editRef
	If editIsNew
		undoKind = 1
	ElseIf originalRef && originalRef != editRef
		undoKind = 2
		undoOld = originalRef ; kept hidden with its record until the next change, so it can come back
		StorageUtil.FormListRemove(None, ListKey, originalRef, True)
		PGNative.StopLive(originalRef)
		SetGlow(originalRef, 0, 0)
		originalRef.Disable()
	Else
		undoKind = 2
		CopyRecord(editRef, editRef, "", UndoPrefix, True)
		uX = origX
		uY = origY
		uZ = origZ
		uAX = origAX
		uAY = origAY
		uAZ = origAZ
		uScale = origScale
	EndIf
	; record it first: moving it can reload its 3D, and OnLoad puts back whatever is recorded
	Remember(editRef)
	EndEdit(True)
	PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
	PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
	ApplyLook(editRef)
	ShowMagic(editRef, curMagic) ; restart it on the painting's final spot
	SetLight(editRef)
	ApplyDayNight(editRef)
	If undoOld
		StorageUtil.SetFormValue(editRef, StashKey, StorageUtil.GetFormValue(undoOld, StashKey)) ; the stash moves to the new record
		StorageUtil.UnsetFormValue(undoOld, StashKey)
		StorageUtil.SetStringValue(editRef, StashNameKey, StorageUtil.GetStringValue(undoOld, StashNameKey))
	EndIf
	UpdateStash(editRef)
	If curSlide > 0
		(editRef as PGPaintingScript).StartSlideshow()
	EndIf
	originalRef = None
	Debug.Notification("Painting placed")
EndFunction

; X while adjusting, or "Take this painting down" in the gallery.
Function TakeDownEdited()
	If editIsNew
		Return
	EndIf
	If ConfirmTakeDown && ConfirmTakeDown.Show() != 0
		Return ; kept: carry on adjusting
	EndIf
	EndEdit(False)
	If editRef != originalRef
		ShowMagic(editRef, 0)
		Discard(editRef)
	EndIf
	SetSize(originalRef, origScale)
	originalRef.SetAngle(origAX, origAY, origAZ)
	originalRef.SetPosition(origX, origY, origZ)
	TakeDown(originalRef)
	editRef = None
	originalRef = None
EndFunction

Function CancelEdit()
	EndEdit(False)
	ShowMagic(editRef, 0)
	If editIsNew || editRef != originalRef
		Discard(editRef)
	EndIf
	If !editIsNew
		originalRef.Enable()
		SetSize(originalRef, origScale)
		originalRef.SetAngle(origAX, origAY, origAZ)
		originalRef.SetPosition(origX, origY, origZ)
		Reapply(originalRef)
		SetLightFromRecord(originalRef) ; the light it had (a light tried while editing goes)
		ObjectReference lightRef = StorageUtil.GetFormValue(originalRef, LightRefKey) as ObjectReference
		If lightRef
			PositionLight(originalRef, lightRef)
			lightRef.Enable()
		EndIf
	EndIf
	editRef = None
	originalRef = None
	Debug.Notification("Cancelled")
EndFunction

; Remove a painting that was never placed (its video and effects stop with it).
Function Discard(ObjectReference ref)
	PGNative.StopLive(ref)
	ObjectReference lightRef = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
	If lightRef ; a light tried on a painting never placed
		lightRef.Disable()
		lightRef.Delete()
		StorageUtil.UnsetFormValue(ref, LightRefKey)
	EndIf
	ref.Disable()
	ref.Delete()
EndFunction

; Swap the edited painting to the record for the current display/frame and the picture's shape.
Function UpdateBase()
	Form base = CurrentBase()
	If !base
		Return
	EndIf
	Bool swapped = editRef.GetBaseObject() != base
	If swapped
		ObjectReference newRef = editRef.PlaceAtMe(base, 1, True)
		PGNative.StopLive(editRef)
		SetGlow(editRef, 0, 0)
		PlaceLight(editRef, -1) ; its light goes with it (a hung one's comes back from its record on Cancel or Undo)
		If editRef == originalRef
			originalRef.Disable()
		Else
			editRef.Disable()
			editRef.Delete()
		EndIf
		editRef = newRef
		SetSize(editRef, scale)
		PGNative.PlaceSetRef(editRef)
	EndIf
	PGNative.ApplyPicture(editRef, curPic, curFilter, curTone)
	PGNative.SetLive(editRef, curPic, curFilter, curTone, curLife)
	If swapped
		ApplyLook(editRef)
		shownMagic = 0
		ShowMagic(editRef, curMagic)
		SetLight(editRef) ; and the light chosen, on the new one
	EndIf
EndFunction

; =============================================================================================
; Undo, copy and paste
; =============================================================================================

; A new change is about to be made: the one Undo reaches now goes into the history (10 changes deep;
; the oldest is then made final).
Function ClearUndo()
	If undoKind == 0 || !undoRef
		undoKind = 0
		undoRef = None
		undoOld = None
		Return
	EndIf
	If !hKind
		hKind = new Int[10]
		hRef = new ObjectReference[10]
		hOld = new ObjectReference[10]
		hTag = new Int[10]
		hPos = new Float[70]
	EndIf
	If hCount >= 10
		FinalUndo(hKind[0], hRef[0], hOld[0], hTag[0])
		Int i = 0
		While i < 9
			hKind[i] = hKind[i + 1]
			hRef[i] = hRef[i + 1]
			hOld[i] = hOld[i + 1]
			hTag[i] = hTag[i + 1]
			Int j = 0
			While j < 7
				hPos[i * 7 + j] = hPos[i * 7 + 7 + j]
				j += 1
			EndWhile
			i += 1
		EndWhile
		hCount = 9
	EndIf
	Int tag = 0
	If undoKind == 2 && !undoOld ; changed in place: its earlier record moves under a key of its own
		nextUndoTag += 1
		tag = nextUndoTag
		CopyRecord(undoRef, undoRef, UndoPrefix, UndoPrefix + tag + "_", True)
		ClearRecord(undoRef, UndoPrefix)
	EndIf
	hKind[hCount] = undoKind
	hRef[hCount] = undoRef
	hOld[hCount] = undoOld
	hTag[hCount] = tag
	Int b = hCount * 7
	hPos[b] = uX
	hPos[b + 1] = uY
	hPos[b + 2] = uZ
	hPos[b + 3] = uAX
	hPos[b + 4] = uAY
	hPos[b + 5] = uAZ
	hPos[b + 6] = uScale
	hCount += 1
	undoKind = 0
	undoRef = None
	undoOld = None
EndFunction

; A change too far back to undo: tidy away what was kept for it.
Function FinalUndo(Int kind, ObjectReference ref, ObjectReference old, Int tag)
	If kind == 3 && ref
		ForgetPainting(ref, True)
		ref.Delete()
	ElseIf kind == 2 && old
		ForgetPainting(old, True)
		old.Delete()
	ElseIf kind == 2 && ref && tag > 0
		ClearRecord(ref, UndoPrefix + tag + "_")
	EndIf
EndFunction

; The newest change in the history becomes the one Undo reaches (a painting since removed is skipped).
Function PopUndo()
	undoKind = 0
	undoRef = None
	undoOld = None
	While undoKind == 0 && hCount > 0
		hCount -= 1
		Int i = hCount
		If hRef[i]
			Int b = i * 7
			undoKind = hKind[i]
			undoRef = hRef[i]
			undoOld = hOld[i]
			uX = hPos[b]
			uY = hPos[b + 1]
			uZ = hPos[b + 2]
			uAX = hPos[b + 3]
			uAY = hPos[b + 4]
			uAZ = hPos[b + 5]
			uScale = hPos[b + 6]
			If undoKind == 2 && !undoOld && hTag[i] > 0
				CopyRecord(undoRef, undoRef, UndoPrefix + hTag[i] + "_", UndoPrefix, True)
				ClearRecord(undoRef, UndoPrefix + hTag[i] + "_")
			EndIf
		EndIf
		hRef[i] = None
		hOld[i] = None
	EndWhile
EndFunction

; What Undo can reach, newest first, for the gallery's undo list.
Function SyncUndoList()
	String[] items = new String[11]
	Int n = 0
	If undoKind != 0 && undoRef
		items[0] = UndoLabel(undoKind, undoRef)
		n = 1
	EndIf
	Int i = hCount - 1
	While i >= 0 && n < 11
		If hRef[i]
			items[n] = UndoLabel(hKind[i], hRef[i])
			n += 1
		EndIf
		i -= 1
	EndWhile
	PGNative.SetUndoList(items)
EndFunction

String Function UndoLabel(Int kind, ObjectReference ref)
	String what = "a painting"
	Int p = FindPictureOf(ref)
	If p >= 0
		what = PGNative.GetPictureName(p)
	EndIf
	If kind == 1
		Return "Hung " + what
	ElseIf kind == 3
		Return "Took down " + what
	EndIf
	Return "Changed " + what
EndFunction

Function Undo()
	If undoKind == 0 || !undoRef
		PopUndo()
	EndIf
	If undoKind == 0 || !undoRef
		Debug.Notification("Nothing to undo")
		undoKind = 0
		Return
	EndIf
	If undoKind == 1 ; placed a new painting: take it away
		ForgetPainting(undoRef, True)
		undoRef.Disable()
		undoRef.Delete()
		Debug.Notification("Undone: painting removed")
	ElseIf undoKind == 3 ; took one down: put it back
		undoRef.Enable()
		ObjectReference undoBox = StorageUtil.GetFormValue(undoRef, StashKey) as ObjectReference
		If undoBox
			undoBox.Enable()
		EndIf
		StorageUtil.FormListAdd(None, ListKey, undoRef, False)
		Reapply(undoRef)
		ObjectReference lightRef = StorageUtil.GetFormValue(undoRef, LightRefKey) as ObjectReference
		If lightRef
			lightRef.Enable()
		EndIf
		If StorageUtil.GetIntValue(undoRef, SlideKey) > 0
			(undoRef as PGPaintingScript).StartSlideshow()
		EndIf
		Debug.Notification("Undone: painting put back")
	ElseIf undoOld ; changed it into another record: bring the old one back
		ForgetPainting(undoRef, True)
		undoRef.Disable()
		undoRef.Delete()
		undoOld.Enable()
		StorageUtil.FormListAdd(None, ListKey, undoOld, False)
		Reapply(undoOld)
		SetLightFromRecord(undoOld)
		Debug.Notification("Undone")
	Else ; changed it in place: its old record and position
		CopyRecord(undoRef, undoRef, UndoPrefix, "", True)
		ClearRecord(undoRef, UndoPrefix)
		SetSize(undoRef, uScale)
		undoRef.SetAngle(uAX, uAY, uAZ)
		undoRef.SetPosition(uX, uY, uZ)
		undoRef = RebuildFromRecord(undoRef, True)
		Debug.Notification("Undone")
	EndIf
	PopUndo() ; the change before it is next
EndFunction

; Copies a painting's record between keys (prefix "" = the live record). withPicture: also the picture,
; nameplate and light; otherwise only its look.
Function CopyRecord(ObjectReference src, ObjectReference dst, String fromPrefix, String toPrefix, Bool withPicture)
	If withPicture
		StorageUtil.SetStringValue(dst, toPrefix + PicKey, StorageUtil.GetStringValue(src, fromPrefix + PicKey))
		StorageUtil.SetStringValue(dst, toPrefix + PicIdKey, StorageUtil.GetStringValue(src, fromPrefix + PicIdKey))
		StorageUtil.SetStringValue(dst, toPrefix + TitleKey, StorageUtil.GetStringValue(src, fromPrefix + TitleKey))
		StorageUtil.SetIntValue(dst, toPrefix + LightKey, StorageUtil.GetIntValue(src, fromPrefix + LightKey))
		StorageUtil.SetIntValue(dst, toPrefix + SlideKey, StorageUtil.GetIntValue(src, fromPrefix + SlideKey))
		StorageUtil.SetIntValue(dst, toPrefix + NightKey, StorageUtil.GetIntValue(src, fromPrefix + NightKey))
		StorageUtil.SetStringValue(dst, toPrefix + NightPicKey, StorageUtil.GetStringValue(src, fromPrefix + NightPicKey))
		StorageUtil.SetStringValue(dst, toPrefix + NightPicIdKey, StorageUtil.GetStringValue(src, fromPrefix + NightPicIdKey))
		CopyPicks(src, fromPrefix + SlidePickKey, dst, toPrefix + SlidePickKey)
	EndIf
	StorageUtil.SetIntValue(dst, toPrefix + FilterKey, StorageUtil.GetIntValue(src, fromPrefix + FilterKey))
	StorageUtil.SetIntValue(dst, toPrefix + ToneKey, StorageUtil.GetIntValue(src, fromPrefix + ToneKey))
	StorageUtil.SetIntValue(dst, toPrefix + LifeKey, StorageUtil.GetIntValue(src, fromPrefix + LifeKey, 1))
	StorageUtil.SetIntValue(dst, toPrefix + MagicKey, StorageUtil.GetIntValue(src, fromPrefix + MagicKey))
	StorageUtil.SetIntValue(dst, toPrefix + GlowKey, StorageUtil.GetIntValue(src, fromPrefix + GlowKey))
	StorageUtil.SetIntValue(dst, toPrefix + DisplayKey, StorageUtil.GetIntValue(src, fromPrefix + DisplayKey))
	StorageUtil.SetIntValue(dst, toPrefix + FrameKey, StorageUtil.GetIntValue(src, fromPrefix + FrameKey))
	StorageUtil.SetIntValue(dst, toPrefix + SizeKey, StorageUtil.GetIntValue(src, fromPrefix + SizeKey, 1))
	Int i = 0
	While i < NumOpts
		StorageUtil.SetIntValue(dst, toPrefix + OptPrefix + i, StorageUtil.GetIntValue(src, fromPrefix + OptPrefix + i, OptDefault(i)))
		i += 1
	EndWhile
EndFunction

; A slideshow's picks (a string list) from one place to another, replacing what was there.
Function CopyPicks(Form src, String srcKey, Form dst, String dstKey)
	StorageUtil.StringListClear(dst, dstKey)
	String[] ids = StorageUtil.StringListToArray(src, srcKey)
	Int i = 0
	While ids && i < ids.Length
		StorageUtil.StringListAdd(dst, dstKey, ids[i], False)
		i += 1
	EndWhile
EndFunction

Function ClearRecord(ObjectReference ref, String prefix)
	StorageUtil.UnsetStringValue(ref, prefix + PicKey)
	StorageUtil.UnsetStringValue(ref, prefix + PicIdKey)
	StorageUtil.UnsetStringValue(ref, prefix + TitleKey)
	StorageUtil.UnsetIntValue(ref, prefix + FilterKey)
	StorageUtil.UnsetIntValue(ref, prefix + ToneKey)
	StorageUtil.UnsetIntValue(ref, prefix + LifeKey)
	StorageUtil.UnsetIntValue(ref, prefix + MagicKey)
	StorageUtil.UnsetIntValue(ref, prefix + GlowKey)
	StorageUtil.UnsetIntValue(ref, prefix + DisplayKey)
	StorageUtil.UnsetIntValue(ref, prefix + FrameKey)
	StorageUtil.UnsetIntValue(ref, prefix + SizeKey)
	StorageUtil.UnsetIntValue(ref, prefix + LightKey)
	StorageUtil.UnsetIntValue(ref, prefix + SlideKey)
	StorageUtil.UnsetIntValue(ref, prefix + NightKey)
	StorageUtil.UnsetStringValue(ref, prefix + NightPicKey)
	StorageUtil.UnsetStringValue(ref, prefix + NightPicIdKey)
	Int i = 0
	While i < NumOpts
		StorageUtil.UnsetIntValue(ref, prefix + OptPrefix + i)
		i += 1
	EndWhile
EndFunction

; Copy look (gallery, Ctrl+C): everything but the picture and the nameplate's words.
Function CopyCurrent()
	EnsureOpts()
	cFilter = curFilter
	cTone = curTone
	cLife = curLife
	cMagic = curMagic
	cGlow = curGlow
	cDisplay = curDisplay
	cFrame = curFrame
	cSize = curSize
	cLight = curLight
	cSlide = curSlide
	cOpt = new Int[75]
	Int i = 0
	While i < NumOpts
		cOpt[i] = curOpt[i]
		i += 1
	EndWhile
	hasCopy = True
EndFunction

; Copy its look (activating a hung painting).
Function CopyFrom(ObjectReference ref)
	cFilter = StorageUtil.GetIntValue(ref, FilterKey)
	cTone = StorageUtil.GetIntValue(ref, ToneKey)
	cLife = StorageUtil.GetIntValue(ref, LifeKey, 1)
	cMagic = StorageUtil.GetIntValue(ref, MagicKey)
	cGlow = StorageUtil.GetIntValue(ref, GlowKey)
	cDisplay = StorageUtil.GetIntValue(ref, DisplayKey)
	cFrame = StorageUtil.GetIntValue(ref, FrameKey)
	cSize = StorageUtil.GetIntValue(ref, SizeKey, 1)
	cLight = StorageUtil.GetIntValue(ref, LightKey)
	cSlide = StorageUtil.GetIntValue(ref, SlideKey)
	cOpt = StoredOpts(ref)
	hasCopy = True
	Debug.Notification("Look copied")
EndFunction

; Paste look (gallery, Ctrl+V) onto the painting being edited.
Function PasteCurrent()
	If !hasCopy
		Debug.Notification("Copy a look first")
		Return
	EndIf
	curFilter = cFilter
	curTone = cTone
	curLife = cLife
	curMagic = cMagic
	curGlow = cGlow
	curDisplay = cDisplay
	curFrame = cFrame
	curSize = cSize
	curLight = cLight
	curSlide = cSlide
	curOpt = new Int[75]
	Int i = 0
	While i < NumOpts
		curOpt[i] = CopiedOpt(i)
		i += 1
	EndWhile
	Int[] sel = Selection()
	i = 1 ; keep the picture (and its day / night pictures): every tab and option, as Selection() lays them out
	While i < 11 + NumOpts
		PGNative.BuildModeSet(i, sel[i])
		i += 1
	EndWhile
	scale = SizeScale(curSize)
	PGNative.PlaceSetScale(scale)
	UpdateBase()
	ApplyLook(editRef)
	ShowMagic(editRef, curMagic)
	SetLight(editRef) ; the copied light, at once
	Debug.Notification("Look pasted")
EndFunction

; A saved style (the gallery already shows its choices): everything but the picture and nameplate words.
Function ApplyValues(Int[] v)
	curFilter = v[1]
	curTone = v[2]
	curLife = v[3]
	curMagic = v[4]
	curDisplay = v[5]
	curFrame = v[6]
	curSize = v[7]
	curLight = v[8]
	curSlide = v[9]
	curGlow = v[10]
	EnsureOpts()
	Int i = 0
	While i < NumOpts && 11 + i < v.Length
		curOpt[i] = v[11 + i]
		i += 1
	EndWhile
	curTone = ToneCode(curTone % 16)
	scale = SizeScale(curSize)
	PGNative.PlaceSetScale(scale)
	UpdateBase()
	ApplyLook(editRef)
	ShowMagic(editRef, curMagic)
	SetLight(editRef) ; the style's light, at once
EndFunction

; Paste the copied look onto a hung painting (activating it).
Function PasteOnto(ObjectReference ref)
	If !hasCopy
		Debug.Notification("Copy a look first")
		Return
	EndIf
	ClearUndo()
	CopyRecord(ref, ref, "", UndoPrefix, True)
	undoKind = 2
	uX = ref.GetPositionX()
	uY = ref.GetPositionY()
	uZ = ref.GetPositionZ()
	uAX = ref.GetAngleX()
	uAY = ref.GetAngleY()
	uAZ = ref.GetAngleZ()
	uScale = ref.GetScale()
	StorageUtil.SetIntValue(ref, FilterKey, cFilter)
	StorageUtil.SetIntValue(ref, ToneKey, cTone)
	StorageUtil.SetIntValue(ref, LifeKey, cLife)
	StorageUtil.SetIntValue(ref, MagicKey, cMagic)
	StorageUtil.SetIntValue(ref, GlowKey, cGlow)
	StorageUtil.SetIntValue(ref, DisplayKey, cDisplay)
	StorageUtil.SetIntValue(ref, FrameKey, cFrame)
	StorageUtil.SetIntValue(ref, SizeKey, cSize)
	StorageUtil.SetIntValue(ref, LightKey, cLight)
	StorageUtil.SetIntValue(ref, SlideKey, cSlide)
	Int i = 0
	While i < NumOpts
		StorageUtil.SetIntValue(ref, OptPrefix + i, CopiedOpt(i))
		i += 1
	EndWhile
	undoRef = RebuildFromRecord(ref, True)
	Debug.Notification("Look pasted")
EndFunction

; Make a hung painting match its record: swaps it to the right record if the display, frame or
; picture shape changed, then puts its picture, look, magic and light back. Returns the painting.
ObjectReference Function RebuildFromRecord(ObjectReference ref, Bool relight)
	Int pic = FindPictureOf(ref)
	Int aspect = 5
	If pic >= 0
		aspect = PGNative.GetPictureAspect(pic)
	EndIf
	Int frame = EffectiveFrame(StorageUtil.GetIntValue(ref, FrameKey), StorageUtil.GetIntValue(ref, MagicKey))
	Form base = BaseFor(TypeIndex(StorageUtil.GetIntValue(ref, DisplayKey), frame), aspect)
	ObjectReference result = ref
	If base && base != ref.GetBaseObject()
		result = ref.PlaceAtMe(base, 1, True)
		SetSize(result, ref.GetScale())
		result.SetAngle(ref.GetAngleX(), ref.GetAngleY(), ref.GetAngleZ())
		result.SetPosition(ref.GetPositionX(), ref.GetPositionY(), ref.GetPositionZ())
		CopyRecord(ref, result, "", "", True)
		StorageUtil.SetFormValue(result, LightRefKey, StorageUtil.GetFormValue(ref, LightRefKey))
		StorageUtil.SetFormValue(result, StashKey, StorageUtil.GetFormValue(ref, StashKey))
		StorageUtil.UnsetFormValue(ref, StashKey)
		StorageUtil.FormListAdd(None, ListKey, result, False)
		ForgetPainting(ref, False)
		ref.Disable()
		ref.Delete()
	EndIf
	Reapply(result)
	PositionStash(result)
	If relight
		SetLightFromRecord(result)
	EndIf
	If StorageUtil.GetIntValue(result, SlideKey) > 0
		(result as PGPaintingScript).StartSlideshow()
	EndIf
	Return result
EndFunction

; =============================================================================================
; Records, lights, slideshow, hide/show, removal
; =============================================================================================

Function Remember(ObjectReference ref)
	EnsureOpts()
	StorageUtil.FormListAdd(None, ListKey, ref, False)
	StorageUtil.SetStringValue(ref, PicKey, PGNative.GetPictureKey(curPic))
	StorageUtil.SetStringValue(ref, PicIdKey, PGNative.GetPictureId(curPic))
	StorageUtil.SetIntValue(ref, FilterKey, curFilter)
	StorageUtil.SetIntValue(ref, ToneKey, curTone)
	StorageUtil.SetIntValue(ref, LifeKey, curLife)
	StorageUtil.SetIntValue(ref, MagicKey, curMagic)
	StorageUtil.SetIntValue(ref, GlowKey, curGlow)
	StorageUtil.SetIntValue(ref, DisplayKey, curDisplay)
	StorageUtil.SetIntValue(ref, FrameKey, curFrame)
	StorageUtil.SetIntValue(ref, SizeKey, curSize)
	StorageUtil.SetIntValue(ref, LightKey, curLight)
	StorageUtil.SetIntValue(ref, SlideKey, curSlide)
	StorageUtil.SetStringValue(ref, TitleKey, curTitle)
	StorageUtil.SetStringValue(ref, StashNameKey, curStashName)
	PGNative.SetStashName(ref, curStashName)
	CopyPicks(self, EditPicksKey, ref, SlidePickKey)
	StorageUtil.SetIntValue(ref, NightKey, curDayNight)
	StorageUtil.SetStringValue(ref, NightPicKey, PGNative.GetPictureKey(curNightPic))
	StorageUtil.SetStringValue(ref, NightPicIdKey, PGNative.GetPictureId(curNightPic))
	Int i = 0
	While i < NumOpts
		StorageUtil.SetIntValue(ref, OptPrefix + i, curOpt[i])
		i += 1
	EndWhile
EndFunction

; A painting's size. The game's own record of it did not always come back with a save (sizes past the scripts' limit
; are written by the plugin), so the size is kept in the gallery's records too and put back when the painting loads.
Function SetSize(ObjectReference ref, Float size)
	If ref
		PGNative.SetRefScale(ref, size)
		StorageUtil.SetFloatValue(ref, "PG_Scale", size)
	EndIf
EndFunction

; Called by a painting when its 3D loads: put its picture and look back on.
Function Reapply(ObjectReference ref)
	Float kept = StorageUtil.GetFloatValue(ref, "PG_Scale")
	If kept > 0.0 && !(editing && ref == editRef)
		Float has = ref.GetScale()
		If has - kept > 0.011 || kept - has > 0.011
			PGNative.SetRefScale(ref, kept)
		EndIf
	EndIf
	Int pic = FindPictureOf(ref)
	If pic >= 0
		PGNative.ApplyPicture(ref, pic, StorageUtil.GetIntValue(ref, FilterKey), StorageUtil.GetIntValue(ref, ToneKey))
		PGNative.SetLive(ref, pic, StorageUtil.GetIntValue(ref, FilterKey), StorageUtil.GetIntValue(ref, ToneKey), StorageUtil.GetIntValue(ref, LifeKey, 1))
	ElseIf StorageUtil.GetStringValue(ref, PicKey) != ""
		PGNative.ShowMissing(ref) ; the file was removed: a parchment says so, and the painting keeps everything else
	EndIf
	; its light, told to the plugin again: after the game is restarted it no longer knows which light is whose,
	; and the light's own colour, strength, reach, flicker and hours were not applied until the painting was edited
	ObjectReference itsLight = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
	If itsLight
		PGNative.LinkLight(ref, itsLight)
	EndIf
	PGNative.SetLook(ref, StoredOpts(ref), StorageUtil.GetStringValue(ref, TitleKey))
	PGNative.SetStashName(ref, StorageUtil.GetStringValue(ref, StashNameKey))
	SetGlow(ref, StorageUtil.GetIntValue(ref, MagicKey), StorageUtil.GetIntValue(ref, GlowKey))
	Int night = -1
	If StorageUtil.GetIntValue(ref, NightKey) == 1
		night = FindNightPictureOf(ref)
	EndIf
	PGNative.SetDayNight(ref, pic, night, StorageUtil.GetIntValue(ref, FilterKey), StorageUtil.GetIntValue(ref, ToneKey), StorageUtil.GetIntValue(ref, LifeKey, 1))
EndFunction

Int Function FindNightPictureOf(ObjectReference ref)
	Return PGNative.FindPicture(StorageUtil.GetStringValue(ref, NightPicKey), StorageUtil.GetStringValue(ref, NightPicIdKey))
EndFunction

; Day and night (Picture page): the plugin swaps to the night picture between dusk and dawn.
Function ApplyDayNight(ObjectReference ref)
	Int night = -1
	If curDayNight == 1
		night = curNightPic
	EndIf
	PGNative.SetDayNight(ref, curPic, night, curFilter, curTone, curLife)
EndFunction

; ---- the hidden stash ----------------------------------------------------------------------------

; The painting swings aside on its left edge and the strongbox behind it opens; it swings back when
; the container closes. The box is made the first time and lives inside the wall behind the painting.
; Displays that hang on a wall (Catalog::HangsOnWall): only these hide a wall safe.
Bool Function HangsOnWall(Int display)
	Return display <= 5 || display == 8 ; solid displays flat on the wall (not a floating page or a projection)
EndFunction

; How long the painting takes to move aside (Finish > Speed): Catalog::kStashSeconds.
Float Function StashSeconds(ObjectReference painting)
	Int speed = OptOf(painting, 58)
	If speed < 0
		speed = 0
	ElseIf speed > 28
		speed = 28
	EndIf
	Return 0.2 + 0.1 * speed ; as the plugin has it (Look.cpp)
EndFunction

Function OpenStash(ObjectReference painting)
	Int opening = StorageUtil.GetIntValue(painting, OptPrefix + OptStashOpen)
	If !StashBase || opening <= 0 || !HangsOnWall(StorageUtil.GetIntValue(painting, DisplayKey))
		Return
	EndIf
	UpdateStash(painting)
	ObjectReference box = StorageUtil.GetFormValue(painting, StashKey) as ObjectReference
	If !box
		Return
	EndIf
	openStyle = opening - 1
	openStash = painting ; set first, so a second press can't open it twice
	openBox = box
	PGNative.SwingPainting(painting, True, openStyle)
	Utility.Wait(StashSeconds(painting))
	PGNative.SafeDoor(box, True) ; then the safe's door cracks open
	Utility.Wait(0.25)
	String name = StorageUtil.GetStringValue(painting, StashNameKey)
	If name == ""
		name = "Hidden stash"
	EndIf
	box.SetDisplayName(name, True) ; the safe has no name of its own: no prompt, no way in but through its painting
	stashMenuSeen = False
	RegisterForMenu("ContainerMenu")
	box.Activate(Game.GetPlayer(), True) ; True: straight to the safe, whatever else listens for activation
	Int tries = 0
	While tries < 15 && !stashMenuSeen && !UI.IsMenuOpen("ContainerMenu") ; up to 3 s for the menu to come up
		Utility.Wait(0.2)
		tries += 1
	EndWhile
	If openStash == painting && !stashMenuSeen && !UI.IsMenuOpen("ContainerMenu")
		; it never opened (the safe couldn't be reached): close everything up, so it can be opened again
		Debug.Notification("The hidden stash wouldn't open - try again")
		UnregisterForMenu("ContainerMenu")
		box.SetDisplayName("", True)
		PGNative.SafeDoor(box, False)
		PGNative.SwingPainting(painting, False, openStyle)
		openStash = None
		openBox = None
	EndIf
EndFunction

Event OnMenuOpen(String menuName)
	If menuName == "ContainerMenu" && openStash
		stashMenuSeen = True
	EndIf
EndEvent

Event OnMenuClose(String menuName)
	If menuName == "ContainerMenu" && openStash
		UnregisterForMenu("ContainerMenu")
		If openBox
			openBox.SetDisplayName("", True)
			PGNative.SafeDoor(openBox, False) ; the door shuts first, then the painting swings back over it
			Utility.Wait(0.25)
		EndIf
		PGNative.SwingPainting(openStash, False, openStyle)
		openStash = None
		openBox = None
	EndIf
EndEvent

; The Finish page's Hidden stash: a painting with one gets its wall safe (made the first time); one without
; hands back whatever was in it and the safe goes.
Function UpdateStash(ObjectReference painting)
	If StorageUtil.GetIntValue(painting, OptPrefix + OptStashOpen) > 0 && HangsOnWall(StorageUtil.GetIntValue(painting, DisplayKey))
		If StashBase && !StorageUtil.GetFormValue(painting, StashKey)
			ObjectReference made = painting.PlaceAtMe(StashBase, 1, True)
			made.SetActorOwner(Game.GetPlayer().GetActorBase()) ; the player's own: never stealing, whoever owns the room
			StorageUtil.SetFormValue(painting, StashKey, made)
		EndIf
		PositionStash(painting)
	Else
		EmptyStash(painting, True)
	EndIf
EndFunction

; A wall safe flush on the wall behind the painting, sized so the painting hides it.
Function PositionStash(ObjectReference painting)
	ObjectReference box = StorageUtil.GetFormValue(painting, StashKey) as ObjectReference
	If !box
		Return
	EndIf
	Float[] t = PGNative.StashSpot(painting)
	box.SetActorOwner(Game.GetPlayer().GetActorBase()) ; older safes too
	If t && t[6] > 0.0
		box.Enable()
		box.SetScale(t[6])
		box.SetAngle(t[3], t[4], t[5])
		box.SetPosition(t[0], t[1], t[2])
	EndIf
EndFunction

; Taking a painting down hands you whatever was in its stash.
Function EmptyStash(ObjectReference painting, Bool remove)
	ObjectReference box = StorageUtil.GetFormValue(painting, StashKey) as ObjectReference
	If !box
		Return
	EndIf
	If box.GetNumItems() > 0
		box.RemoveAllItems(Game.GetPlayer(), True, True)
		Debug.Notification("You take what was hidden behind the painting")
	EndIf
	If remove
		box.Disable()
		box.Delete()
		StorageUtil.UnsetFormValue(painting, StashKey)
	EndIf
EndFunction

; The picture a painting shows, wherever its file is now. A renamed or moved file is found by its
; fingerprint, and the record is updated so it is found straight away next time. -1 if it is gone.
Int Function FindPictureOf(ObjectReference ref)
	String savedKey = StorageUtil.GetStringValue(ref, PicKey)
	String savedId = StorageUtil.GetStringValue(ref, PicIdKey)
	Int pic = PGNative.FindPicture(savedKey, savedId)
	If pic >= 0
		String nowKey = PGNative.GetPictureKey(pic)
		String nowId = PGNative.GetPictureId(pic)
		If nowKey != savedKey || nowId != savedId
			StorageUtil.SetStringValue(ref, PicKey, nowKey)
			StorageUtil.SetStringValue(ref, PicIdKey, nowId)
		EndIf
	EndIf
	Return pic
EndFunction

; Put a magic glow on a painting in a glow style. Classic plays one of Skyrim's own effect shaders
; over the whole painting (as the first versions did); the others are the native glow pieces.
Function SetGlow(ObjectReference ref, Int magic, Int glow)
	StopClassic(ref)
	PGNative.SetMagic(ref, magic, glow)
EndFunction

Function StopClassic(ObjectReference ref)
	Int i = MagicList.GetSize()
	While i > 0
		i -= 1
		(MagicList.GetAt(i) as EffectShader).Stop(ref)
	EndWhile
EndFunction

; Frame "Auto": a frame that suits the magic (or dark wood without magic).
Int Function EffectiveFrame(Int frame, Int magic)
	If frame != AutoFrame
		Return frame
	ElseIf magic == 1 ; enchanted
		Return 13 ; enchanted gilt
	ElseIf magic == 2 ; arcane gold
		Return 1 ; gilded
	ElseIf magic == 3 ; ethereal
		Return 8 ; Solitude silver
	ElseIf magic == 4 ; Daedric violet
		Return 9 ; Daedric iron
	ElseIf magic == 5 ; ember
		Return 4 ; Dwemer bronze
	ElseIf magic == 6 ; soul gem
		Return 2 ; black lacquer
	ElseIf magic == 7 ; frost
		Return 18 ; stalhrim
	ElseIf magic == 8 ; storm
		Return 16 ; glass
	ElseIf magic == 9 ; Sovngarde
		Return 3 ; Nordic carved
	ElseIf magic == 10 ; nocturnal
		Return 17 ; ebony
	EndIf
	Return 0
EndFunction

; Index in LightList: each colour has dim, normal, bright. Colours: the four lights, then one per magic.
Int Function LightIndex(Int type, Int level, Int magic)
	If type <= 0
		Return -1
	ElseIf type == MatchMagicLight
		If magic <= 0
			Return level ; no magic to match: warm
		EndIf
		Return 12 + (magic - 1) * 3 + level
	EndIf
	Return (type - 1) * 3 + level
EndFunction

; The painting's light from the current choices (a lamp brings its own light if none is chosen).
Function SetLight(ObjectReference painting)
	EnsureOpts()
	Int type = curLight
	If type == 0 && curOpt[OptLamp] > 0
		type = 2 ; candle sconces: candlelight
		If curOpt[OptLamp] == 1
			type = 1 ; the brass picture light: warm lamp
		EndIf
	EndIf
	PlaceLight(painting, LightIndex(type, curOpt[OptLightLevel], curMagic))
EndFunction

Function SetLightFromRecord(ObjectReference painting)
	Int type = StorageUtil.GetIntValue(painting, LightKey)
	Int lamp = StorageUtil.GetIntValue(painting, OptPrefix + OptLamp)
	If type == 0 && lamp > 0
		type = 2 ; candle sconces: candlelight
		If lamp == 1
			type = 1 ; the brass picture light: warm lamp
		EndIf
	EndIf
	PlaceLight(painting, LightIndex(type, StorageUtil.GetIntValue(painting, OptPrefix + OptLightLevel, 1), StorageUtil.GetIntValue(painting, MagicKey)))
EndFunction

Function PlaceLight(ObjectReference painting, Int index)
	ObjectReference old = StorageUtil.GetFormValue(painting, LightRefKey) as ObjectReference
	If old
		old.Disable()
		old.Delete()
		StorageUtil.UnsetFormValue(painting, LightRefKey)
	EndIf
	If index < 0 || index >= LightList.GetSize()
		Return
	EndIf
	ObjectReference lightRef = painting.PlaceAtMe(LightList.GetAt(index), 1, True)
	PositionLight(painting, lightRef)
	PGNative.LinkLight(painting, lightRef) ; the player's colour, strength and reach
	StorageUtil.SetFormValue(painting, LightRefKey, lightRef)
EndFunction

; In front of the painting and a little above its centre, further out for big paintings.
Function PositionLight(ObjectReference painting, ObjectReference lightRef)
	PGNative.LinkLight(painting, lightRef)
	Float s = painting.GetScale()
	Float az = painting.GetAngleZ()
	Int outStep = OptOf(painting, 48)
	If outStep < 2
		outStep = 2
	EndIf
	Float out = 45.0 * s * outStep / 10.0 ; Distance
	If out < 20.0
		out = 20.0
	EndIf
	Float side = (OptOf(painting, 46) - 10) * 6.0 * s ; Left / right, as you face the painting
	Float lift = 30.0 * s + (OptOf(painting, 47) - 10) * 6.0 * s ; Height
	lightRef.SetPosition(painting.GetPositionX() - Math.Sin(az) * out + Math.Cos(az) * side, painting.GetPositionY() - Math.Cos(az) * out - Math.Sin(az) * side, painting.GetPositionZ() + lift)
EndFunction

; Seconds between slides for a painting (Slideshow page).
Float Function SlideSeconds(ObjectReference ref)
	Int i = StorageUtil.GetIntValue(ref, OptPrefix + OptSlideTime, 1)
	If i == 0
		Return 5.0
	ElseIf i == 2
		Return 30.0
	ElseIf i == 3
		Return 60.0
	ElseIf i == 4
		Return 300.0
	ElseIf i == 5
		Return 900.0
	ElseIf i == 6
		Return 3600.0
	EndIf
	Return 10.0
EndFunction

; Called by a painting's slideshow timer: show the next picture (in order, shuffled, favourites or
; the same album), keeping its display, frame, filter and the rest of its look.
Function NextSlide(ObjectReference ref)
	If editing && (ref == editRef || ref == originalRef)
		(ref as PGPaintingScript).StartSlideshow()
		Return
	EndIf
	If PGNative.GetPictureCount() < 2
		Return
	EndIf
	Int mode = StorageUtil.GetIntValue(ref, SlideKey)
	Int pic = -1
	If mode == 5 ; My picks: the next ticked picture after this one (stills only)
		pic = NextPick(ref)
	Else
		pic = PGNative.NextSlidePicture(FindPictureOf(ref), mode)
	EndIf
	If pic < 0
		(ref as PGPaintingScript).StartSlideshow()
		Return
	EndIf
	StorageUtil.SetStringValue(ref, PicKey, PGNative.GetPictureKey(pic))
	StorageUtil.SetStringValue(ref, PicIdKey, PGNative.GetPictureId(pic))
	RebuildFromRecord(ref, False) ; same canvas shape: just the picture changes; otherwise the record is swapped
EndFunction

Int Function NextPick(ObjectReference ref)
	String[] ids = StorageUtil.StringListToArray(ref, SlidePickKey)
	If !ids || ids.Length == 0
		Return -1
	EndIf
	String now = StorageUtil.GetStringValue(ref, PicIdKey)
	Int at = ids.Find(now)
	Int n = 0
	While n < ids.Length
		at += 1
		If at >= ids.Length
			at = 0
		EndIf
		Int pic = PGNative.FindPicture("", ids[at])
		If pic >= 0 && !PGNative.IsVideo(pic) && ids[at] != now
			Return pic
		EndIf
		n += 1
	EndWhile
	Return -1
EndFunction

; Hide or show every painting (and its light) in the player's cell or within reach outdoors.
Function SetAreaHidden(Bool hide)
	Actor player = Game.GetPlayer()
	Cell here = player.GetParentCell()
	Int i = StorageUtil.FormListCount(None, ListKey)
	Int n = 0
	While i > 0
		i -= 1
		ObjectReference ref = StorageUtil.FormListGet(None, ListKey, i) as ObjectReference
		If !ref
			StorageUtil.FormListRemoveAt(None, ListKey, i)
		ElseIf ref.GetParentCell() == here || player.GetDistance(ref) < 4000.0
			ObjectReference lightRef = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
			ObjectReference box = StorageUtil.GetFormValue(ref, StashKey) as ObjectReference
			If hide
				ref.Disable()
				If lightRef
					lightRef.Disable()
				EndIf
				If box
					box.Disable() ; the safe hides with its painting - a bare safe would give the stash away
				EndIf
			Else
				ref.Enable()
				If lightRef
					lightRef.Enable()
				EndIf
				If box
					box.Enable()
				EndIf
			EndIf
			n += 1
		EndIf
	EndWhile
	If hide
		Debug.Notification(n + " paintings hidden")
	Else
		Debug.Notification(n + " paintings shown")
	EndIf
EndFunction

; Show every hidden painting everywhere (MCM).
Function ShowAll()
	Int i = StorageUtil.FormListCount(None, ListKey)
	While i > 0
		i -= 1
		ObjectReference ref = StorageUtil.FormListGet(None, ListKey, i) as ObjectReference
		If ref
			ref.Enable()
			ObjectReference lightRef = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
			If lightRef
				lightRef.Enable()
			EndIf
			ObjectReference box = StorageUtil.GetFormValue(ref, StashKey) as ObjectReference
			If box
				box.Enable() ; its safe, hidden with it
			EndIf
		EndIf
	EndWhile
EndFunction

Int Function PaintingCount()
	Return StorageUtil.FormListCount(None, ListKey)
EndFunction

; After updating the mod (mod menu): every painting made again from its own record - its model for the display and
; frame it has, its picture, look, glow, light and stash - so nothing has to be taken down and hung again.
Int Function RefreshAll()
	If editing
		CancelEdit()
	EndIf
	Form[] all = StorageUtil.FormListToArray(None, ListKey)
	Int i = 0
	Int n = 0
	While i < all.Length
		ObjectReference ref = all[i] as ObjectReference
		If ref && ref.IsEnabled()
			ObjectReference now = RebuildFromRecord(ref, True)
			UpdateStash(now)
			n += 1
		EndIf
		i += 1
	EndWhile
	Return n
EndFunction

; Before uninstalling (mod menu): every painting, light and wall safe goes, what was in the safes comes back to
; the player, the spell is taken away and the gallery stops listening. After a save the mod can be removed.
Function RemoveEverything()
	If editing
		CancelEdit()
	EndIf
	PGNative.Browse(False)
	Int i = StorageUtil.FormListCount(None, ListKey)
	Int n = 0
	While i > 0
		i -= 1
		ObjectReference ref = StorageUtil.FormListGet(None, ListKey, i) as ObjectReference
		If ref
			ForgetPainting(ref, True) ; its video, glow, light and safe (the safe's things come back to you)
			ref.Disable()
			ref.Delete()
			n += 1
		EndIf
	EndWhile
	StorageUtil.FormListClear(None, ListKey)
	; what Undo was keeping hidden (paintings taken down, earlier versions)
	FinalUndo(undoKind, undoRef, undoOld, 0)
	While hCount > 0
		hCount -= 1
		FinalUndo(hKind[hCount], hRef[hCount], hOld[hCount], hTag[hCount])
		hRef[hCount] = None
		hOld[hCount] = None
	EndWhile
	undoKind = 0
	undoRef = None
	undoOld = None
	If previewLight
		previewLight.Disable()
		previewLight.Delete()
		previewLight = None
	EndIf
	If AnimateSpell
		Game.GetPlayer().RemoveSpell(AnimateSpell)
	EndIf
	UnregisterForAllKeys()
	UnregisterForAllModEvents()
	Debug.MessageBox("Personal Gallery: " + n + " paintings removed.\n\nSave your game now, quit, and then remove the mod. (To keep using it instead, just load a save or restart the game.)")
EndFunction

; Taking down can be undone until the next change: the painting is hidden, then removed for good.
Function TakeDown(ObjectReference ref)
	ClearUndo()
	StorageUtil.FormListRemove(None, ListKey, ref, True)
	PGNative.StopLive(ref)
	SetGlow(ref, 0, 0)
	ref.Disable()
	ObjectReference lightRef = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
	If lightRef
		lightRef.Disable()
	EndIf
	EmptyStash(ref, False)
	ObjectReference box = StorageUtil.GetFormValue(ref, StashKey) as ObjectReference
	If box
		box.Disable() ; no bare safe left on the wall (Undo brings both back)
	EndIf
	undoKind = 3
	undoRef = ref
	Debug.Notification("Painting taken down (Undo puts it back)")
EndFunction

; Drop a painting from the records; optionally remove its light too.
Function ForgetPainting(ObjectReference ref, Bool removeLight)
	PGNative.StopLive(ref)
	SetGlow(ref, 0, 0)
	If removeLight
		ObjectReference lightRef = StorageUtil.GetFormValue(ref, LightRefKey) as ObjectReference
		If lightRef
			lightRef.Disable()
			lightRef.Delete()
		EndIf
	EndIf
	If removeLight
		EmptyStash(ref, True)
	EndIf
	StorageUtil.FormListRemove(None, ListKey, ref, True)
	StorageUtil.UnsetFormValue(ref, LightRefKey)
	StorageUtil.UnsetFloatValue(ref, "PG_Scale")
	ClearRecord(ref, "")
	ClearRecord(ref, UndoPrefix)
EndFunction

; =============================================================================================
; Helpers
; =============================================================================================

ObjectReference Function FindNearest()
	ObjectReference best = None
	Float bestDist = 2000.0
	Actor player = Game.GetPlayer()
	Int i = StorageUtil.FormListCount(None, ListKey)
	While i > 0
		i -= 1
		ObjectReference ref = StorageUtil.FormListGet(None, ListKey, i) as ObjectReference
		If ref && ref.IsEnabled()
			Float d = player.GetDistance(ref)
			If d < bestDist
				bestDist = d
				best = ref
			EndIf
		EndIf
	EndWhile
	If !best
		Debug.Notification("No painting nearby")
	EndIf
	Return best
EndFunction

; Record type for a display (and frame): framed x frames, then the frameless displays, then table x frames.
Int Function TypeIndex(Int display, Int frame)
	If frame >= NumFrames
		frame = 0
	EndIf
	If display == 0
		Return frame
	ElseIf display == 7
		Return NumFrames + NumSimpleDisplays + frame
	ElseIf display > 7
		Return NumFrames + display - 2 ; mirror, orb, page come after the table
	EndIf
	Return NumFrames + display - 1
EndFunction

Form Function BaseFor(Int type, Int aspect)
	FormList list = TypeLists.GetAt(type) as FormList
	If !list
		Return None
	EndIf
	Return list.GetAt(aspect)
EndFunction

Form Function CurrentBase()
	Return BaseFor(TypeIndex(curDisplay, EffectiveFrame(curFrame, curMagic)), PGNative.GetPictureAspect(curPic))
EndFunction

; Swap the magic glow on the painting being edited.
Function ShowMagic(ObjectReference ref, Int magic)
	shownMagic = magic
	SetGlow(ref, magic, curGlow)
EndFunction

; Animate Portrait spell: wake a living portrait under the crosshair, or put it to sleep.
Function AnimateCast(ObjectReference ref)
	Int awake = PGNative.ToggleAwake(ref)
	If awake < 0
		Debug.Notification("There is no life in this painting to wake")
	ElseIf awake == 1
		(MagicList.GetAt(1) as EffectShader).Play(ref, 2.0) ; illusion shimmer as it stirs
		Debug.Notification("The portrait stirs")
	Else
		Debug.Notification("The portrait falls still")
	EndIf
EndFunction

Float Function SizeScale(Int i)
	If i == 0
		Return 0.5
	ElseIf i == 2
		Return 2.0
	ElseIf i == 3
		Return 4.0
	ElseIf i == 4
		Return 8.0
	EndIf
	Return 1.0
EndFunction

Float Function ClampF(Float v, Float lo, Float hi)
	If v < lo
		Return lo
	ElseIf v > hi
		Return hi
	EndIf
	Return v
EndFunction
