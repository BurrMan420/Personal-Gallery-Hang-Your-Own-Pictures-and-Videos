Scriptname PGConfigMenu extends SKI_ConfigBase
{Personal Gallery: Mod Configuration Menu.}

PGControlScript Property Gallery Auto

Event OnPageReset(String page)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Controls")
	AddKeyMapOptionST("MenuKeyST", "Gallery key", Gallery.MenuKey)
	AddToggleOptionST("HelpST", "Show the full help when building", Gallery.AlwaysShowHelp)
	If Gallery.HasNative()
		AddToggleOptionST("SnapST", "Snap new paintings to surfaces", Gallery.SnapByDefault)
	Else
		AddTextOption("Snap to surfaces", "needs SKSE plugin", OPTION_FLAG_DISABLED)
	EndIf
	AddEmptyOption()
	AddHeaderOption("Changes")
	AddTextOptionST("UndoST", "Undo last change", "Undo")
	AddEmptyOption()
	AddHeaderOption("Living portraits")
	AddSliderOptionST("VolumeST", "Sound volume", Gallery.VideoVolume * 100.0, "{0}%")
	AddSliderOptionST("MaxPlayingST", "Portraits moving at once", Gallery.MaxPlaying as Float, "{0}")
	AddMenuOptionST("QualityST", "Video quality limit", QualityName(Gallery.VideoQualityLimit))
	AddMenuOptionST("FpsST", "Video frame rate", FpsName(Gallery.VideoFps))
	AddEmptyOption()
	AddHeaderOption("Performance")
	AddToggleOptionST("ShadowST", "Paintings cast shadows", !Gallery.NoShadows)
	AddTextOptionST("CacheST", "Picture cache on disk", PGNative.GetCacheMB() + " MB - clear")
	AddEmptyOption()
	AddHeaderOption("Uninstalling")
	AddTextOptionST("UninstallST", "Remove everything first", "Remove")

	SetCursorPosition(1)
	AddHeaderOption("Your gallery")
	AddTextOption("Paintings hung", Gallery.PaintingCount() as String, OPTION_FLAG_DISABLED)
	AddTextOption("Pictures available", PGNative.GetPictureCount() as String, OPTION_FLAG_DISABLED)
	AddTextOption("Add pictures in", "Data\\PersonalGallery\\Pictures", OPTION_FLAG_DISABLED)
	AddTextOptionST("ScanST", "Scan for new pictures", "Scan")
	AddTextOptionST("ShowAllST", "Show every hidden painting", "Go")
	AddTextOptionST("RefreshST", "Refresh every painting", "Refresh")
	AddEmptyOption()
	AddHeaderOption("Keys while building")
	keyOptions = new Int[13]
	keyOptions[0] = AddKeyMapOption("Place it (also a click)", Gallery.GetHotkey(0))
	keyOptions[1] = AddKeyMapOption("Cancel (also a right-click)", Gallery.GetHotkey(1))
	keyOptions[2] = AddKeyMapOption("Gallery panel / back", Gallery.GetHotkey(2))
	keyOptions[3] = AddKeyMapOption("What the wheel changes", Gallery.GetHotkey(3))
	keyOptions[4] = AddKeyMapOption("Lock the spot (arrows slide)", Gallery.GetHotkey(4))
	keyOptions[5] = AddKeyMapOption("Snap / free placement", Gallery.GetHotkey(5))
	keyOptions[6] = AddKeyMapOption("Align with nearby paintings", Gallery.GetHotkey(6))
	keyOptions[7] = AddKeyMapOption("Grid steps", Gallery.GetHotkey(7))
	keyOptions[8] = AddKeyMapOption("Reset the wheel's setting", Gallery.GetHotkey(8))
	keyOptions[9] = AddKeyMapOption("Take it down (when adjusting)", Gallery.GetHotkey(9))
	keyOptions[10] = AddKeyMapOption("Mute a video while placing", Gallery.GetHotkey(10))
	keyOptions[11] = AddKeyMapOption("Pause a video while placing", Gallery.GetHotkey(11))
	keyOptions[12] = AddKeyMapOption("New painting (gallery mode)", Gallery.GetHotkey(12))
	AddTextOption("Size, rotate, tilt, depth", "Mouse wheel", OPTION_FLAG_DISABLED)
	AddTextOption("Fine adjustment", "Hold Ctrl", OPTION_FLAG_DISABLED)
	AddTextOption("Undo, copy look, paste look", "Ctrl+Z, Ctrl+C, Ctrl+V", OPTION_FLAG_DISABLED)
EndEvent

Int[] keyOptions ; the key rows above, by action

Event OnOptionKeyMapChange(Int option, Int keyCode, String conflictControl, String conflictName)
	Int slot = keyOptions.Find(option)
	If slot < 0
		Return
	EndIf
	Int other = 0
	While other < 13 ; two gallery actions on one key: say so, and ask
		If other != slot && Gallery.GetHotkey(other) == keyCode
			If !ShowMessage("Another gallery action already uses this key.\n\nUse it for this one as well? (Only the first of the two will answer.)", True, "Yes", "No")
				Return
			EndIf
			other = 13
		EndIf
		other += 1
	EndWhile
	Gallery.SetHotkey(slot, keyCode)
	SetKeyMapOptionValue(option, keyCode)
EndEvent

Event OnOptionDefault(Int option)
	Int slot = keyOptions.Find(option)
	If slot >= 0
		Gallery.SetHotkey(slot, Gallery.HotkeyDefault(slot))
		SetKeyMapOptionValue(option, Gallery.HotkeyDefault(slot))
	EndIf
EndEvent

Event OnOptionHighlight(Int option)
	If keyOptions && keyOptions.Find(option) >= 0
		SetInfoText("Click, then press the key you want for this while building. The hints on screen show your keys. Press the default key (R by default in this menu) to put it back.")
	EndIf
EndEvent

State MenuKeyST
	Event OnKeyMapChangeST(Int newKeyCode, String conflictControl, String conflictName)
		If conflictControl != ""
			String msg = "This key is already used by " + conflictControl
			If conflictName != ""
				msg += " (" + conflictName + ")"
			EndIf
			If !ShowMessage(msg + ".\n\nUse it anyway?", True, "Yes", "No")
				Return
			EndIf
		EndIf
		Gallery.SetMenuKey(newKeyCode)
		SetKeyMapOptionValueST(newKeyCode)
	EndEvent
	Event OnDefaultST()
		Gallery.SetMenuKey(199)
		SetKeyMapOptionValueST(199)
	EndEvent
	Event OnHighlightST()
		SetInfoText("Starts build mode. Look at one of your paintings to move it; anywhere else hangs a new one. While building, Tab (or this key) opens the gallery panel.")
	EndEvent
EndState

State HelpST
	Event OnSelectST()
		Gallery.AlwaysShowHelp = !Gallery.AlwaysShowHelp
		SetToggleOptionValueST(Gallery.AlwaysShowHelp)
	EndEvent
	Event OnDefaultST()
		Gallery.AlwaysShowHelp = False
		SetToggleOptionValueST(False)
	EndEvent
	Event OnHighlightST()
		SetInfoText("The on-screen hints show every key while you build. Turn this on to also see the full help box once per session.")
	EndEvent
EndState

State SnapST
	Event OnSelectST()
		Gallery.SnapByDefault = !Gallery.SnapByDefault
		SetToggleOptionValueST(Gallery.SnapByDefault)
	EndEvent
	Event OnDefaultST()
		Gallery.SnapByDefault = True
		SetToggleOptionValueST(True)
	EndEvent
	Event OnHighlightST()
		SetInfoText("New paintings land flat on the wall, floor or ceiling you aim at. Press Q while placing to switch between snapping and free placement.")
	EndEvent
EndState

State RefreshST
	Event OnSelectST()
		SetTextOptionValueST("Working...")
		Int n = Gallery.RefreshAll()
		SetTextOptionValueST(n + " refreshed")
	EndEvent
	Event OnHighlightST()
		SetInfoText("Use after updating Personal Gallery: every painting is made again from its own settings, with its light and stash, so you don't have to take them down and hang them again. Hidden paintings are left alone.")
	EndEvent
EndState

State ShadowST
	Event OnSelectST()
		Gallery.NoShadows = !Gallery.NoShadows
		PGNative.SetShadows(!Gallery.NoShadows)
		SetToggleOptionValueST(!Gallery.NoShadows)
	EndEvent
	Event OnDefaultST()
		Gallery.NoShadows = False
		PGNative.SetShadows(True)
		SetToggleOptionValueST(True)
	EndEvent
	Event OnHighlightST()
		SetInfoText("Whether your paintings throw shadows on the wall and floor. Off is a little lighter on your PC and stops very large paintings darkening a room.")
	EndEvent
EndState

State CacheST
	Event OnSelectST()
		If ShowMessage("Clear the picture cache?\n\nThe painted looks and filtered videos on disk are removed. Nothing is lost: each is made again the next time a painting needs it, which takes a moment per painting.", True, "Clear it", "Keep it")
			PGNative.ClearCache()
			SetTextOptionValueST("0 MB")
		EndIf
	EndEvent
	Event OnHighlightST()
		SetInfoText("How much disk space the gallery's made textures and filtered videos take. Looks you tried and left behind are tidied away by themselves after two weeks.")
	EndEvent
EndState

State UninstallST
	Event OnSelectST()
		If ShowMessage("Remove every painting, its light and its hidden stash?\n\nWhatever is in the stashes comes back to you. This can't be undone. Do it before uninstalling Personal Gallery, then save.", True, "Remove everything", "Cancel")
			Gallery.RemoveEverything()
			ForcePageReset()
		EndIf
	EndEvent
	Event OnHighlightST()
		SetInfoText("For a clean uninstall: takes down every painting, light and wall safe, returns what the safes held, and stops the gallery. Then save, quit and remove the mod.")
	EndEvent
EndState

Event OnGameReload()
	parent.OnGameReload()
	Gallery.PushSettings() ; the native plugin starts fresh each session
	Gallery.GameReloaded() ; and the paintings in sight get their pictures back
EndEvent

State VolumeST
	Event OnSliderOpenST()
		SetSliderDialogStartValue(Gallery.VideoVolume * 100.0)
		SetSliderDialogDefaultValue(80.0)
		SetSliderDialogRange(0.0, 100.0)
		SetSliderDialogInterval(5.0)
	EndEvent
	Event OnSliderAcceptST(Float value)
		Gallery.VideoVolume = value / 100.0
		Gallery.PushSettings()
		SetSliderOptionValueST(value, "{0}%")
	EndEvent
	Event OnDefaultST()
		Gallery.VideoVolume = 0.8
		Gallery.PushSettings()
		SetSliderOptionValueST(80.0, "{0}%")
	EndEvent
	Event OnHighlightST()
		SetInfoText("How loud living portraits are. Their sound comes from the painting and fades with distance.")
	EndEvent
EndState

String Function QualityName(Int i)
	If i <= 0
		Return "Low (480)"
	ElseIf i == 1
		Return "Medium (720)"
	ElseIf i == 2
		Return "High (960)"
	ElseIf i == 3
		Return "Ultra (1440)"
	EndIf
	Return "Full HD (1920)"
EndFunction

String Function FpsName(Int i)
	If i <= 0
		Return "15 fps"
	ElseIf i == 1
		Return "24 fps"
	ElseIf i == 2
		Return "30 fps"
	EndIf
	Return "60 fps (smoothest, most work)"
EndFunction

State QualityST
	Event OnMenuOpenST()
		String[] names = new String[5]
		Int i = 0
		While i < 5
			names[i] = QualityName(i)
			i += 1
		EndWhile
		SetMenuDialogOptions(names)
		SetMenuDialogStartIndex(Gallery.VideoQualityLimit)
		SetMenuDialogDefaultIndex(2)
	EndEvent
	Event OnMenuAcceptST(Int index)
		Gallery.VideoQualityLimit = index
		Gallery.PushSettings()
		SetMenuOptionValueST(QualityName(index))
	EndEvent
	Event OnDefaultST()
		Gallery.VideoQualityLimit = 2
		Gallery.PushSettings()
		SetMenuOptionValueST(QualityName(2))
	EndEvent
	Event OnHighlightST()
		SetInfoText("The sharpest any video painting plays. Lower it on older graphics cards or with many videos at once. Each painting's own Quality still applies below it.")
	EndEvent
EndState

State FpsST
	Event OnMenuOpenST()
		String[] names = new String[4]
		names[0] = FpsName(0)
		names[1] = FpsName(1)
		names[2] = FpsName(2)
		names[3] = FpsName(3)
		SetMenuDialogOptions(names)
		SetMenuDialogStartIndex(Gallery.VideoFps)
		SetMenuDialogDefaultIndex(2)
	EndEvent
	Event OnMenuAcceptST(Int index)
		Gallery.VideoFps = index
		Gallery.PushSettings()
		SetMenuOptionValueST(FpsName(index))
	EndEvent
	Event OnDefaultST()
		Gallery.VideoFps = 2
		Gallery.PushSettings()
		SetMenuOptionValueST(FpsName(2))
	EndEvent
	Event OnHighlightST()
		SetInfoText("How smoothly video paintings move. 15 or 24 frames a second take less work - good for slower PCs or many videos. Sound is never affected.")
	EndEvent
EndState

State MaxPlayingST
	Event OnSliderOpenST()
		SetSliderDialogStartValue(Gallery.MaxPlaying as Float)
		SetSliderDialogDefaultValue(2.0)
		SetSliderDialogRange(1.0, 16.0)
		SetSliderDialogInterval(1.0)
	EndEvent
	Event OnSliderAcceptST(Float value)
		Gallery.MaxPlaying = value as Int
		Gallery.PushSettings()
		SetSliderOptionValueST(value, "{0}")
	EndEvent
	Event OnDefaultST()
		Gallery.MaxPlaying = 2
		Gallery.PushSettings()
		SetSliderOptionValueST(2.0, "{0}")
	EndEvent
	Event OnHighlightST()
		SetInfoText("Only the nearest portraits move at once; the rest show their still. More costs more CPU.")
	EndEvent
EndState

State UndoST
	Event OnSelectST()
		Gallery.Undo()
		SetTextOptionValueST("Done")
	EndEvent
	Event OnHighlightST()
		SetInfoText("Undoes the last painting you placed, changed, pasted a look onto or took down. Also: Undo in the gallery, or Ctrl+Z while building.")
	EndEvent
EndState

State ScanST
	Event OnSelectST()
		SetTextOptionValueST("Scanning...")
		Int[] found = PGNative.ScanPictures()
		SetTextOptionValueST("Done")
		If found[0] == 0 && found[1] == 0 && found[2] == 0
			ShowMessage("No changes - your gallery is up to date.", False)
		Else
			ShowMessage(found[0] + " new, " + found[1] + " removed, " + found[2] + " renamed or moved.\n\nNew pictures are in the gallery now.", False)
		EndIf
		ForcePageReset()
	EndEvent
	Event OnHighlightST()
		SetInfoText("Added pictures or videos while playing? Scan picks them up without restarting. Renamed or moved pictures stay on their paintings; removed ones show a 'picture missing' parchment. The game also scans every time it starts.")
	EndEvent
EndState

State DetailST
	Event OnSelectST()
		Gallery.HighDetail = !Gallery.HighDetail
		Gallery.PushSettings()
		SetToggleOptionValueST(Gallery.HighDetail)
	EndEvent
	Event OnDefaultST()
		Gallery.HighDetail = False
		Gallery.PushSettings()
		SetToggleOptionValueST(False)
	EndEvent
	Event OnHighlightST()
		SetInfoText("Off (recommended): pictures at 2048 - sharp on any painting and light on video memory. On: 4096 for huge murals; uses four times the memory. Paintings pick it up the next time they load.")
	EndEvent
EndState

State ShowAllST
	Event OnSelectST()
		Gallery.ShowAll()
		SetTextOptionValueST("Done")
	EndEvent
	Event OnHighlightST()
		SetInfoText("Brings back every painting you hid with 'Hide paintings here', everywhere.")
	EndEvent
EndState
