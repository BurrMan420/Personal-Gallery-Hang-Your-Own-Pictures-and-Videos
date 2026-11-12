Scriptname PGAnimateEffect extends ActiveMagicEffect
{Personal Gallery: Animate Portrait. Cast while looking at a living portrait to wake it or put it to sleep.}

PGControlScript Property Gallery Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	ObjectReference ref = PGNative.PaintingInView(3000.0)
	If ref && (ref as PGPaintingScript)
		Gallery.AnimateCast(ref)
	Else
		Debug.Notification("Look at a portrait as you cast")
	EndIf
EndEvent
