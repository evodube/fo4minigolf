#pragma once

#include "sim/Sim.h"

// Data\F4SE\Plugins\fo4minigolf.ini. Read when the game's data is loaded and again at the start of every round, so
// edits apply without restarting the game.
namespace mg
{
	struct Settings
	{
		static Settings& Get()
		{
			static Settings s;
			return s;
		}

		void Load();

		// a key's name for the key hints ("X", "F7")
		static std::string Key(int a_vk);

		// [Keys] virtual-key codes
		int keyLeave{ 0x58 };     // X: end the round
		int keyOverhead{ 0x51 };  // Q: the view from above the hole (the right mouse button does it too)

		// [Putting]
		float pullPerCount{ 0.0035f };  // the putt's power per mouse count pulled back (0..1 is none to full)
		bool  invertPull{ false };      // push the mouse forward for power instead
		float powerCurve{ 1.5f };       // power -> speed: speed = max * power ^ this (finer control of soft putts)
		int   maxStrokes{ 6 };          // a hole's over at this many (scored as this many)
		bool  guide{ true };            // the dots showing where the putt goes
		float guideLength{ 120.0f };    // ...how far ahead they show (game units)

		// [Camera]
		float camDistance{ 85.0f };   // behind the ball
		float camPitch{ 28.0f };      // degrees down, to start with
		float camMinPitch{ 6.0f };
		float camMaxPitch{ 75.0f };

		// [Sound]
		bool  sound{ true };
		float soundVolume{ 0.8f };

		// [Physics] (Sim.h's Spec)
		golf::Spec spec;

		// the plugin's file name
		std::string plugin{ "fo4minigolf.esl" };
	};
}
