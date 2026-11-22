#include "Settings.h"

namespace mg
{
	namespace
	{
		constexpr auto kIni = L"Data\\F4SE\\Plugins\\fo4minigolf.ini";

		float ReadFloat(const wchar_t* a_section, const wchar_t* a_key, float a_default)
		{
			wchar_t buf[64]{};
			::GetPrivateProfileStringW(a_section, a_key, L"", buf, 64, kIni);
			if (!buf[0]) {
				return a_default;
			}
			try {
				return std::stof(buf);
			} catch (...) {
				return a_default;
			}
		}

		int ReadInt(const wchar_t* a_section, const wchar_t* a_key, int a_default)
		{
			wchar_t buf[64]{};
			::GetPrivateProfileStringW(a_section, a_key, L"", buf, 64, kIni);
			if (!buf[0]) {
				return a_default;
			}
			try {
				return static_cast<int>(std::stol(buf, nullptr, 0));  // (0x58 and 88 alike)
			} catch (...) {
				return a_default;
			}
		}
	}

	void Settings::Load()
	{
		keyPlay = ReadInt(L"Keys", L"iPlay", 0x67);
		keyLeave = ReadInt(L"Keys", L"iLeave", 0x58);
		keyOverhead = ReadInt(L"Keys", L"iOverhead", 0x51);

		pullPerCount = std::clamp(ReadFloat(L"Putting", L"fPullPerCount", 0.0035f), 0.0002f, 0.05f);
		invertPull = ReadInt(L"Putting", L"bInvertPull", 0) != 0;
		powerCurve = std::clamp(ReadFloat(L"Putting", L"fPowerCurve", 1.5f), 1.0f, 3.0f);
		maxStrokes = std::clamp(ReadInt(L"Putting", L"iMaxStrokes", 6), 2, 20);
		guide = ReadInt(L"Putting", L"bGuide", 1) != 0;
		guideLength = std::clamp(ReadFloat(L"Putting", L"fGuideLength", 60.0f), 0.0f, 1000.0f);
		guideFlipY = ReadInt(L"Putting", L"bGuideFlipY", 1) != 0;

		oppSkill = std::clamp(ReadFloat(L"Opponent", L"fSkill", 0.35f), 0.0f, 1.0f);
		oppRange = std::clamp(ReadFloat(L"Opponent", L"fRange", 2000.0f), 200.0f, 10000.0f);
		oppHidden = ReadInt(L"Opponent", L"bHide", 1) != 0;

		camDistance = std::clamp(ReadFloat(L"Camera", L"fDistance", 85.0f), 20.0f, 400.0f);
		camPitch = std::clamp(ReadFloat(L"Camera", L"fPitch", 28.0f), 0.0f, 89.0f);
		camMinPitch = std::clamp(ReadFloat(L"Camera", L"fMinPitch", 6.0f), -20.0f, 89.0f);
		camMaxPitch = std::clamp(ReadFloat(L"Camera", L"fMaxPitch", 75.0f), camMinPitch, 89.0f);

		sound = ReadInt(L"Sound", L"bSound", 1) != 0;
		soundVolume = std::clamp(ReadFloat(L"Sound", L"fVolume", 0.8f), 0.0f, 2.0f);

		golf::Spec d;
		spec.rollDecel = std::clamp(ReadFloat(L"Physics", L"fRollDecel", static_cast<float>(d.rollDecel)), 5.0f, 200.0f);
		spec.maxSpeed = std::clamp(ReadFloat(L"Physics", L"fMaxSpeed", static_cast<float>(d.maxSpeed)), 100.0f, 1500.0f);
		spec.landRestitution = std::clamp(ReadFloat(L"Physics", L"fLandBounce", static_cast<float>(d.landRestitution)), 0.0f, 0.9f);
		log::info("settings: leave {:X}, pull {:.4f}{}, max strokes {}, camera {:.0f} at {:.0f} degrees", keyLeave, pullPerCount,
			invertPull ? " (inverted)" : "", maxStrokes, camDistance, camPitch);
	}

	std::string Settings::Key(int a_vk)
	{
		switch (a_vk) {
		case VK_LBUTTON: return "LEFT BUTTON";
		case VK_RBUTTON: return "RIGHT BUTTON";
		case VK_TAB: return "TAB";
		case VK_SPACE: return "SPACE";
		case VK_RETURN: return "ENTER";
		case VK_BACK: return "BACKSPACE";
		default: break;
		}
		if ((a_vk >= '0' && a_vk <= '9') || (a_vk >= 'A' && a_vk <= 'Z')) {
			return std::string(1, static_cast<char>(a_vk));
		}
		if (a_vk >= VK_F1 && a_vk <= VK_F12) {
			return std::format("F{}", a_vk - VK_F1 + 1);
		}
		if (a_vk >= VK_NUMPAD0 && a_vk <= VK_NUMPAD9) {
			return std::format("NUMPAD {}", a_vk - VK_NUMPAD0);
		}
		return std::format("KEY {:X}", a_vk);
	}
}
