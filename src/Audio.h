#pragma once

namespace mg::Audio
{
	enum class Bank
	{
		kPutt,    // the putter hitting the ball
		kWall,    // the ball off a rail or an obstacle
		kRim,     // the ball catching the cup's rim
		kCup,     // the ball dropping into the cup
		kSplash,  // into the water
		kLand,    // landing after a jump
		kHoleIn,  // a little fanfare: holed (louder for a hole in one)
		kCount
	};

	// Loads Data\Sound\fo4minigolf\*.wav (<bank>_<n>.wav, mono 16-bit 44.1 kHz) and starts XAudio2. Safe to call more
	// than once.
	void Init();

	// how far the course's sounds carry (game units, about 70 a metre)
	inline constexpr float kNear = 700.0f;

	// Plays one of the bank's sounds at a_pos in the world, quieter with distance from the camera (silent past
	// a_range). a_gain 0..1. a_seconds > 0 plays only that much of it, fading out at the end.
	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, float a_seconds = 0.0f, float a_range = kNear);

	// As Play, with the loudness following a_keys over time: (seconds from the start, gain 0..1) pairs, in order,
	// joined by straight lines; the sound stops at the last one.
	void PlayShaped(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, const std::vector<std::pair<float, float>>& a_keys,
		float a_range = kNear);

	// At the listener: no position.
	void PlayUI(Bank a_bank, float a_gain);

	// Which way the listener faces (radians, the game's yaw). Set each frame while playing.
	void SetListenerYaw(float a_yaw, bool a_valid);
}
