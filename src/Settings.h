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
		int keyPlay{ 0x67 };      // Numpad 7: start a round on the nearest course (besides activating it)
		int keyLeave{ 0x58 };     // X: end the round
		int keyOverhead{ 0x51 };  // Q: the view from above the hole (the right mouse button does it too)

		// [Putting]
		float pullPerCount{ 0.0035f };  // the putt's power per mouse count pulled back (0..1 is none to full)
		bool  invertPull{ false };      // push the mouse forward for power instead
		float powerCurve{ 1.5f };       // power -> speed: speed = max * power ^ this (finer control of soft putts)
		int   maxStrokes{ 6 };          // a hole's over at this many (scored as this many)
		bool  guide{ true };            // the green aim line from the ball (drawn on the HUD, as Bowling's)
		float guideLength{ 60.0f };     // ...how far it reaches (game units: about 85 cm)
		bool  guideFlipY{ true };       // (if the line shows up mirrored top to bottom, set 0)

		// [Controller] an Xbox-style controller (XInput), alongside the mouse and keyboard: the right stick aims (the
		// view, as the mouse), hold RT and pull the left stick back for power, let go of RT to putt (B cancels a putt);
		// Y the view from above; hold B a second to end the round; hold A on the course to pick who plays (D-pad, A, B)
		bool  controller{ true };
		float padDeadzone{ 0.22f };    // how far a stick moves before it counts (0..0.9)
		float padPowerSpeed{ 1.0f };   // power a second with the stick pulled all the way back (1 = full)
		bool  padInvertPull{ false };  // pushing the stick forward gives power instead

		// [Opponent] a settler played 1v1 (hold the activate key on the course)
		float oppSkill{ 0.35f };     // 0..1: how true their putts are (1: the physics' own best shot, near enough)
		float oppRange{ 2000.0f };   // how near you someone has to be to be asked (game units)
		bool  oppHidden{ true };     // they're invisible while the round lasts (their score still shows)

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
