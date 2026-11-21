#include "Audio.h"
#include "CameraHook.h"
#include "Forms.h"
#include "Golf.h"
#include "Hook.h"
#include "Hud.h"
#include "MenuWatch.h"

namespace
{
	constexpr auto kName = "fo4minigolf";

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			mg::menus::Watch();
			mg::Hud::Install();  // F4SE Menu Framework is loaded by now, if it's installed
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			mg::log::info("game data ready");
			mg::Golf::Get().Init();
			mg::Forms::AddWorkshopMenu();
			mg::menus::Watch();
			mg::Hook::Install();
			mg::CameraHook::Install();
			mg::Audio::Init();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			mg::Golf::Get().Abort("a save loading");
			break;
		case F4SE::MessagingInterface::kPreSaveGame:
			mg::Golf::Get().BeforeSave();
			break;
		case F4SE::MessagingInterface::kPostSaveGame:
			mg::Golf::Get().AfterSave();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			mg::Hook::Install();  // no-op if already hooked
			mg::CameraHook::Install();
			break;
		default:
			break;
		}
	}
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kName;
	a_info->version = 1;

	if (a_f4se->IsEditor()) {
		return false;
	}
	return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .log = true, .logName = kName, .logRotate = 3, .trampoline = false });

	mg::log::info("fo4minigolf loading on runtime {} ({})", a_f4se->RuntimeVersion().string(),
		REX::FModule::IsRuntimeOG() ? "OG" : REX::FModule::IsRuntimeNG() ? "NG" : "AE");

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		mg::log::error("could not register F4SE message listener");
		return false;
	}
	mg::log::info("loaded; waiting for game data");
	return true;
}
