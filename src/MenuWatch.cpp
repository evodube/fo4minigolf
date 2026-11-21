#include "MenuWatch.h"

namespace mg::menus
{
	namespace
	{
		constexpr std::array<std::string_view, kCount> kNames{ "PauseMenu", "PipboyMenu", "LoadingMenu", "MainMenu", "Console",
			"WorkshopMenu", "DialogueMenu" };

		std::array<std::atomic<bool>, kCount> g_open{};

		// (CommonLibF4 only declares it) the game's own layout: the menu's name, and whether it's opening
		struct EventData
		{
			RE::BSFixedString menuName;
			bool              opening;
		};

		// The Steam overlay (Shift+Tab): the game doesn't pause for it, so the games are paused here while it's up. Told by
		// Steam's own GameOverlayActivated callback, registered through the game's steam_api64.dll (laid out as the
		// Steamworks SDK's CCallbackBase, so its vtable is Steam's)
		std::atomic<bool> g_overlay{ false };

		class SteamCallbackBase
		{
		public:
			SteamCallbackBase() = default;
			virtual void Run(void* a_param) = 0;
			virtual void Run(void* a_param, bool a_ioFailure, std::uint64_t a_call) = 0;
			int          GetICallback() { return iCallback; }
			virtual int  GetCallbackSizeBytes() = 0;

		protected:
			std::uint8_t callbackFlags{ 0 };
			int          iCallback{ 0 };
		};

		class OverlayCallback final : public SteamCallbackBase
		{
		public:
			static constexpr int kGameOverlayActivated = 331;  // k_iSteamFriendsCallbacks + 31

			void Run(void* a_param) override
			{
				if (a_param) {
					const bool up = *static_cast<const std::uint8_t*>(a_param) != 0;  // (m_bActive)
					if (g_overlay.exchange(up) != up) {
						log::info("menus: the Steam overlay {}", up ? "up: games paused" : "down");
					}
				}
			}
			void Run(void* a_param, bool, std::uint64_t) override { Run(a_param); }
			int  GetCallbackSizeBytes() override { return 8; }
		};

		void WatchSteamOverlay()
		{
			static OverlayCallback callback;
			auto*                  steam = ::GetModuleHandleW(L"steam_api64.dll");
			using register_t = void (*)(SteamCallbackBase*, int);
			auto* reg = steam ? reinterpret_cast<register_t>(::GetProcAddress(steam, "SteamAPI_RegisterCallback")) : nullptr;
			if (!reg) {
				log::info("menus: no steam_api64.dll here: the Steam overlay isn't watched");
				return;
			}
			reg(&callback, OverlayCallback::kGameOverlayActivated);
			log::info("menus: watching the Steam overlay");
		}

		struct Sink final : RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto& e = reinterpret_cast<const EventData&>(a_event);
				const char* name = e.menuName.c_str();
				if (!name) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (std::size_t i = 0; i < kCount; ++i) {
					if (kNames[i] == name) {
						g_open[i] = e.opening;
						break;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Watch()
	{
		static Sink sink;
		static bool registered = false;
		auto*       ui = RE::UI::GetSingleton();
		if (registered || !ui) {
			return;
		}
		registered = true;
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
		// what's already open (the main menu, usually): asked once, here, then only the events
		for (std::size_t i = 0; i < kCount; ++i) {
			g_open[i] = ui->GetMenuOpen(RE::BSFixedString(kNames[i]));
		}
		log::info("menus: watching open/close events");
		WatchSteamOverlay();
	}

	bool Open(Id a_id) noexcept
	{
		return a_id < kCount && g_open[a_id].load(std::memory_order_relaxed);
	}

	bool AnyBlocking() noexcept
	{
		if (g_overlay.load(std::memory_order_relaxed)) {
			return true;
		}
		for (const auto id : { kPause, kPipboy, kLoading, kMain, kConsole, kWorkshop }) {
			if (Open(id)) {
				return true;
			}
		}
		return false;
	}
}
