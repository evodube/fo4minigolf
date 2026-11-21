// Hooks the free camera ("tfc") state: while you play, the view is the free camera, orbiting the ball (or high over
// the hole).
//
// Mouse movement reaches the view through the player's LookHandler (look input plus a smoothing spring) and the free
// camera's own mouse handler. While you charge a putt both drop it: the mouse's pull is the putt's power instead
// (Golf::OnLookMouse). GetTranslation is where the game reads the camera position for the frame; Golf puts it
// behind the ball there. (From Wasteland Wagers and Playable Pool.)
//
// Every camera state's update also turns the course's moving obstacles (Golf::Animate), once a frame, playing or not.

#include "CameraHook.h"

#include "Golf.h"

namespace mg::CameraHook
{
	namespace
	{
		using Update_t = void (*)(RE::FreeCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&);
		using GetTranslation_t = void (*)(const RE::FreeCameraState*, RE::NiPoint3&);
		using MouseMove_t = void (*)(RE::FreeCameraState*, const RE::MouseMoveEvent*);

		Update_t         g_update{ nullptr };
		GetTranslation_t g_getTranslation{ nullptr };
		MouseMove_t      g_mouseMove{ nullptr };
		bool             g_installed{ false };

		using LookMouse_t = void (*)(RE::LookHandler*, const RE::MouseMoveEvent*);
		LookMouse_t g_lookMouse{ nullptr };

		// The player's look handler: where FO4 turns mouse movement into look input (and a smoothing spring).
		void HookedLookMouse(RE::LookHandler* a_this, const RE::MouseMoveEvent* a_event)
		{
			if (Golf::Get().OnLookMouse(a_event)) {
				return;  // charging a putt: the movement became power and never reaches the camera
			}
			g_lookMouse(a_this, a_event);
		}

		void HookedMouseMove(RE::FreeCameraState* a_this, const RE::MouseMoveEvent* a_event)
		{
			if (Golf::Get().OnLookMouse(a_event)) {
				return;
			}
			const auto before = a_this->rotation;
			g_mouseMove(a_this, a_event);
			Golf::Get().OnFreeCameraLook(a_this, before);
		}

		void HookedUpdate(RE::FreeCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			Golf::Get().BeforeFreeCameraUpdate(a_this);
			const auto before = a_this->rotation;
			g_update(a_this, a_next);
			Golf::Get().OnFreeCameraLook(a_this, before);
			Golf::Get().OnFreeCameraUpdate(a_this);
			Golf::Get().Animate();
		}

		// every other camera state's update (first person, third person, furniture...): the obstacles turned there too,
		// once a frame (off the game's own tick they'd judder)
		struct OtherUpdate
		{
			void**   vtable{ nullptr };
			Update_t original{ nullptr };
		};
		std::array<OtherUpdate, RE::CameraStates::kTotal> g_other{};

		void HookedOtherUpdate(RE::TESCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			auto** vt = *reinterpret_cast<void***>(a_this);
			for (const auto& o : g_other) {
				if (o.vtable == vt && o.original) {
					reinterpret_cast<void (*)(RE::TESCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&)>(o.original)(a_this, a_next);
					break;
				}
			}
			Golf::Get().Animate();
		}

		void HookedGetTranslation(const RE::FreeCameraState* a_this, RE::NiPoint3& a_out)
		{
			if (!Golf::Get().FreeCameraTranslation(a_this, a_out)) {
				g_getTranslation(a_this, a_out);
			}
		}

		// The menu-opening keys (the Pip-Boy, the workshop...): while you play, the keys that are ours don't get here
		using MenuButton_t = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);
		MenuButton_t g_menuButton{ nullptr };

		void HookedMenuButton(RE::BSInputEventUser* a_this, const RE::ButtonEvent* a_event)
		{
			if (Golf::Get().SwallowButton(a_event)) {
				return;
			}
			g_menuButton(a_this, a_event);
		}

		// Every frame's input, as the menus and the player's controls get it: while you play, our keys' events are emptied
		// first, so no handler acts on them: the game's (the workshop, VATS, anything bound to them) nor any other mod's on
		// the menus' handler list (MCM puts its keybinds there)
		using Process_t = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
		Process_t g_menuProcess{ nullptr };
		Process_t g_playerProcess{ nullptr };

		void DisableOurKeys(const RE::InputEvent* a_head)
		{
			for (auto* e = a_head; e; e = e->next) {
				if (e->eventType.get() == RE::INPUT_EVENT_TYPE::kButton) {
					auto* b = static_cast<const RE::ButtonEvent*>(e);
					if (Golf::Get().SwallowButton(b)) {
						// disabled for the game's own handlers, and no key at all for anyone else's on the menus' list
						// (MCM's keybinds read the key code itself: Workshop Framework's workshop hotkey is one)
						auto* m = const_cast<RE::ButtonEvent*>(b);
						m->disabled = true;
						m->idCode = 0;
					}
				}
			}
		}

		void HookedMenuProcess(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_head)
		{
			DisableOurKeys(a_head);
			g_menuProcess(a_this, a_head);
		}

		void HookedPlayerProcess(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_head)
		{
			DisableOurKeys(a_head);
			g_playerProcess(a_this, a_head);
		}
	}

	namespace
	{
		// a vtable slot swapped for ours: the old one back
		std::uintptr_t SwapSlot(void** a_vtable, std::size_t a_index, const void* a_hook)
		{
			DWORD old = 0;
			if (!::VirtualProtect(&a_vtable[a_index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
				return 0;
			}
			const auto was = reinterpret_cast<std::uintptr_t>(a_vtable[a_index]);
			a_vtable[a_index] = const_cast<void*>(a_hook);
			::VirtualProtect(&a_vtable[a_index], sizeof(void*), old, &old);
			return was;
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		if (REX::FModule::IsRuntimeOG()) {
			// 1.10.163: the vtables by their Address Library IDs
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::FreeCameraState[0] };
			g_mouseMove = reinterpret_cast<MouseMove_t>(vtable.write_vfunc(0x06, &HookedMouseMove));
			g_update = reinterpret_cast<Update_t>(vtable.write_vfunc(0x0B, &HookedUpdate));
			g_getTranslation = reinterpret_cast<GetTranslation_t>(vtable.write_vfunc(0x0D, &HookedGetTranslation));
			REL::Relocation<std::uintptr_t> look{ RE::VTABLE::LookHandler[0] };
			g_lookMouse = reinterpret_cast<LookMouse_t>(look.write_vfunc(0x06, &HookedLookMouse));
			REL::Relocation<std::uintptr_t> menuOpen{ RE::VTABLE::MenuOpenHandler[0] };
			g_menuButton = reinterpret_cast<MenuButton_t>(menuOpen.write_vfunc(0x08, &HookedMenuButton));
		} else {
			// next-gen / Anniversary: the library only has the old game's vtable IDs, so the vtables are read off the
			// live objects instead (the player camera's free camera state, the player's look handler)
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* controls = RE::PlayerControls::GetSingleton();
			auto* free = camera ? camera->cameraStates[RE::CameraStates::kFree].get() : nullptr;
			auto* look = controls ? controls->lookHandler : nullptr;
			if (!free || !look) {
				log::error("free camera hooks: the camera or look handler isn't there yet");
				return;
			}
			auto** freeTable = *reinterpret_cast<void***>(free);
			auto** lookTable = *reinterpret_cast<void***>(look);
			g_mouseMove = reinterpret_cast<MouseMove_t>(SwapSlot(freeTable, 0x06, reinterpret_cast<const void*>(&HookedMouseMove)));
			g_update = reinterpret_cast<Update_t>(SwapSlot(freeTable, 0x0B, reinterpret_cast<const void*>(&HookedUpdate)));
			g_getTranslation = reinterpret_cast<GetTranslation_t>(SwapSlot(freeTable, 0x0D, reinterpret_cast<const void*>(&HookedGetTranslation)));
			g_lookMouse = reinterpret_cast<LookMouse_t>(SwapSlot(lookTable, 0x06, reinterpret_cast<const void*>(&HookedLookMouse)));
			if (!g_mouseMove || !g_update || !g_getTranslation || !g_lookMouse) {
				log::error("free camera hooks: couldn't write the vtables");
				return;
			}
		}
		// the other camera states' updates (off the live objects; each vtable once)
		if (auto* camera = RE::PlayerCamera::GetSingleton()) {
			int         n = 0;
			auto* const freeState = camera->cameraStates[RE::CameraStates::kFree].get();
			void** const freeVt = freeState ? *reinterpret_cast<void***>(freeState) : nullptr;
			for (int s = 0; s < RE::CameraStates::kTotal; ++s) {
				if (s == RE::CameraStates::kFree) {
					continue;
				}
				auto* state = camera->cameraStates[s].get();
				if (!state) {
					continue;
				}
				auto** vt = *reinterpret_cast<void***>(state);
				if (std::ranges::any_of(g_other, [&](const OtherUpdate& a_o) { return a_o.vtable == vt; }) || vt == freeVt) {
					continue;
				}
				const auto was = SwapSlot(vt, 0x0B, reinterpret_cast<const void*>(&HookedOtherUpdate));
				if (was) {
					g_other[s] = { vt, reinterpret_cast<Update_t>(was) };
					++n;
				}
			}
			log::info("camera hooks: {} other camera states' updates", n);
		}
		// (off the live objects: the same on every runtime)
		if (auto* menus = RE::MenuControls::GetSingleton()) {
			g_menuProcess = reinterpret_cast<Process_t>(SwapSlot(*reinterpret_cast<void***>(menus), 0, reinterpret_cast<const void*>(&HookedMenuProcess)));
		}
		if (auto* player = RE::PlayerControls::GetSingleton()) {
			g_playerProcess = reinterpret_cast<Process_t>(SwapSlot(*reinterpret_cast<void***>(static_cast<RE::BSInputEventReceiver*>(player)), 0,
				reinterpret_cast<const void*>(&HookedPlayerProcess)));
		}
		log::info("input hooks: menus {}, player controls {}", g_menuProcess ? "on" : "not found", g_playerProcess ? "on" : "not found");
		g_installed = true;
		log::info("free camera hooks installed");
	}
}
