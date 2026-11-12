Scriptname PGPaintingScript extends ObjectReference
{Personal Gallery: a hung painting. Activating it opens the adjust menu; runs its slideshow timer.}

PGControlScript Property Gallery Auto

Event OnActivate(ObjectReference akActionRef)
	If akActionRef == Game.GetPlayer()
		Gallery.PaintingActivated(self)
	EndIf
EndEvent

Event OnLoad()
	Gallery.Reapply(self) ; the picture is a runtime texture, so put it back each time the 3D loads
	If StorageUtil.GetIntValue(self, Gallery.SlideKey) > 0
		StartSlideshow()
	EndIf
EndEvent

Event OnUnload()
	UnregisterForUpdate()
EndEvent

Function StartSlideshow()
	RegisterForSingleUpdate(Gallery.SlideSeconds(self))
EndFunction

Event OnUpdate()
	If StorageUtil.GetIntValue(self, Gallery.SlideKey) > 0 && Is3DLoaded() && IsEnabled()
		Gallery.NextSlide(self)
	EndIf
EndEvent
