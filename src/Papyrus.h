#pragma once

// A few Papyrus functions called on the player or the settler you play (they run on the script thread, a moment later).
namespace mg::Papyrus
{
	// ObjectReference.SetPosition(x, y, z)
	bool SetPosition(RE::Actor* a_actor, const RE::NiPoint3& a_pos);
	// ObjectReference.SetAngle(x, y, z): degrees
	bool SetAngle(RE::Actor* a_actor, float a_x, float a_y, float a_z);
	// Actor.SetRestrained(bool): held where they are (a settler you're playing)
	bool SetRestrained(RE::Actor* a_actor, bool a_restrained);
	// Actor.EvaluatePackage(): their own AI again
	bool EvaluatePackage(RE::Actor* a_actor);
}
