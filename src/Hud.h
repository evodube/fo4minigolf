#pragma once

namespace mg::Hud
{
	// Registers the HUD with F4SE Menu Framework, if it's installed. Safe to call more than once.
	void Install();

	// The terminal HUD is up (Menu Framework there, its fonts loaded).
	bool Showing();
}
