// The mini golf course in the game: see Golf.h.

#include "Golf.h"

#include "Audio.h"
#include "Forms.h"
#include "MenuWatch.h"
#include "Papyrus.h"
#include "Settings.h"
#include "Util.h"

#include <random>
#include <sstream>
#include <thread>

namespace mg
{
	namespace
	{
		constexpr float  kDegToRad = 0.017453292f;
		constexpr float  kOverheadPitch = 89.0f * kDegToRad;
		constexpr float  kMaxFrame = 0.05f;
		constexpr float  kScanEvery = 0.5f;
		constexpr float  kScanRange = 8000.0f;
		constexpr float  kPenaltySeconds = 1.4f;
		constexpr float  kHoledSeconds = 3.2f;
		constexpr float  kDoneSeconds = 7.0f;
		constexpr std::size_t kMaxDots = 40;      // the guide line's segments (Dot.nif: a 3-unit piece of green line)
		constexpr double kTau = 6.283185307179586;

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		RE::FreeCameraState* FreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			return camera ? static_cast<RE::FreeCameraState*>(camera->cameraStates[RE::CameraStates::kFree].get()) : nullptr;
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = FreeCamera();
			return camera && free && camera->currentState.get() == free;
		}

		float& Component(RE::FreeCameraState* a_state, int a_index)
		{
			return a_index == 0 ? a_state->rotation.x : a_state->rotation.y;
		}

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		// ---- rotations: standard 3x3 matrices (columns: where the local axes go), and the game's (rows: the same)

		using M3 = std::array<std::array<double, 3>, 3>;

		M3 Mul(const M3& a, const M3& b)
		{
			M3 m{};
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					m[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
				}
			}
			return m;
		}

		// a turn of a_angle about the unit axis a_u (right hand rule)
		M3 AxisAngle(double ux, double uy, double uz, double a_angle)
		{
			const double c = std::cos(a_angle), s = std::sin(a_angle), t = 1.0 - c;
			return { { { t * ux * ux + c, t * ux * uy - s * uz, t * ux * uz + s * uy },
				{ t * ux * uy + s * uz, t * uy * uy + c, t * uy * uz - s * ux },
				{ t * ux * uz - s * uy, t * uy * uz + s * ux, t * uz * uz + c } } };
		}

		M3 RotZ(double a_angle) { return AxisAngle(0, 0, 1, a_angle); }

		// the game's matrix (rows: the axes)
		RE::NiMatrix3 Ni(const M3& m)
		{
			return util::FromAxes({ static_cast<float>(m[0][0]), static_cast<float>(m[1][0]), static_cast<float>(m[2][0]) },
				{ static_cast<float>(m[0][1]), static_cast<float>(m[1][1]), static_cast<float>(m[2][1]) },
				{ static_cast<float>(m[0][2]), static_cast<float>(m[1][2]), static_cast<float>(m[2][2]) });
		}

		// ---- events: activations, loading screens, workshop mode

		struct ActivateSink final : RE::BSTEventSink<RE::TESActivateEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent& a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (a_event.objectActivated && a_event.actionRef && a_event.actionRef.get() == player && Forms::IsCourse(a_event.objectActivated.get())) {
					auto handle = a_event.objectActivated->GetHandle();
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([handle]() {
							if (auto ref = handle.get()) {
								Golf::Get().Activated(ref.get());
							}
						});
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// (CommonLibF4 only declares it) the game's own layout: the menu's name, and whether it's opening
		struct MenuOpenCloseEventData
		{
			RE::BSFixedString menuName;
			bool              opening;
		};

		struct LoadingSink final : RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto& e = reinterpret_cast<const MenuOpenCloseEventData&>(a_event);
				if (e.opening && e.menuName.c_str() && std::string_view(e.menuName.c_str()) == "LoadingMenu" && Golf::Get().Playing()) {
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([]() { Golf::Get().Abort("a loading screen"); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// workshop mode started while you play (Workshop Framework's hotkey, anything else that starts it): out again
		struct WorkshopSink final : RE::BSTEventSink<RE::Workshop::WorkshopModeEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::Workshop::WorkshopModeEvent& a_event, RE::BSTEventSource<RE::Workshop::WorkshopModeEvent>*) override
			{
				if (a_event.start && Golf::Get().Playing()) {
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([]() { RE::Workshop::RequestExitWorkshop(false); });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	// ---------------------------------------------------------------- setting up

	void Golf::Init()
	{
		Settings::Get().Load();
		if (!_loaded) {
			const char* paths[] = { "Data\\F4SE\\Plugins\\fo4minigolf\\course.txt", "F4SE\\Plugins\\fo4minigolf\\course.txt" };
			for (const char* path : paths) {
				std::ifstream file(path, std::ios::binary);
				if (!file) {
					continue;
				}
				std::stringstream text;
				text << file.rdbuf();
				std::string error;
				if (golf::Course::Parse(text.str(), _course, error)) {
					_loaded = true;
					std::size_t obstacles = 0;
					for (const auto& h : _course.holes) {
						obstacles += h.obstacles.size();
					}
					log::info("course: {} holes, {} moving obstacles (from {})", _course.holes.size(), obstacles, path);
				} else {
					log::error("course: {}: {}", path, error);
				}
				break;
			}
			if (!_loaded) {
				log::error("course: Data\\F4SE\\Plugins\\fo4minigolf\\course.txt not found or unreadable: the course can't be played");
			}
		}
		static ActivateSink activate;
		static LoadingSink  loading;
		static WorkshopSink workshop;
		static bool         registered = false;
		if (!registered) {
			registered = true;
			if (auto* source = RE::TESActivateEvent::GetEventSource()) {
				source->RegisterSink(&activate);
			} else {
				log::warn("no activate event source: the course can't be played");
			}
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->RegisterSink<RE::MenuOpenCloseEvent>(&loading);
			}
			RE::Workshop::RegisterForWorkshopModeEvent(&workshop);
		}
	}

	// ---------------------------------------------------------------- render thread

	void Golf::OnPresent()
	{
		PollInput();
		if (_tickPending.exchange(true)) {
			return;
		}
		auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			_tickPending = false;
			return;
		}
		tasks->AddTask([this]() {
			Tick();
			_tickPending = false;
		});
	}

	void Golf::PollInput()
	{
		// the game's activate key, down or up (a course activated: tapped plays solo, held plays a settler)
		if (const int vk = _activateVk.load(); vk > 0 && vk < 256) {
			const bool down = util::GameHasFocus() && (::GetAsyncKeyState(vk) & 0x8000) != 0;
			const bool was = _activateDown.load();
			if (down && !was) {
				_activateDownAt = NowMs();
			} else if (!down && was) {
				_activateUpAt = NowMs();
			}
			_activateDown = down;
		}
		if (!_playing) {
			// not playing: only the play key (a round on the nearest course)
			const int vk = Settings::Get().keyPlay;
			const bool now = vk > 0 && vk < 256 && util::GameHasFocus() && !menus::AnyBlocking() && (::GetAsyncKeyState(vk) & 0x8000) != 0;
			const bool was = vk > 0 && vk < 256 && _keysWas[vk];
			if (vk > 0 && vk < 256) {
				_keysWas[vk] = now;
			}
			if (now && !was) {
				if (auto* tasks = F4SE::GetTaskInterface()) {
					tasks->AddTask([this]() { PlayNearest(); });
				}
			}
			_lmbWas = _rmbWas = false;
			return;
		}
		if (!util::GameHasFocus() || menus::AnyBlocking()) {
			_lmbWas = _rmbWas = false;
			if (_charging) {
				_charging = false;  // (a menu came up mid-charge: no putt)
			}
			_keysWas.fill(false);
			return;
		}
		auto*      tasks = F4SE::GetTaskInterface();
		const auto down = [](int a_vk) { return a_vk > 0 && a_vk < 256 && (::GetAsyncKeyState(a_vk) & 0x8000) != 0; };
		const bool lmb = down(VK_LBUTTON);
		const bool rmb = down(VK_RBUTTON);
		if (lmb && !_lmbWas && _phase == Phase::kAim && !_overhead && !_oppTurn) {
			_power = 0.0f;
			_charging = true;  // the mouse's pull is the putt's power now (OnLookMouse)
		}
		if (!lmb && _lmbWas && _charging) {
			const float power = _power.load();
			_charging = false;
			if (tasks) {
				tasks->AddTask([this, power]() { Putt(power); });
			}
		}
		if (rmb && !_rmbWas && !_charging) {
			_overhead = !_overhead;
		}
		_lmbWas = lmb;
		_rmbWas = rmb;

		const auto& s = Settings::Get();
		const auto  pressed = [&](int a_vk) {
            if (a_vk <= 0 || a_vk >= 256) {
                return false;
            }
            const bool now = down(a_vk);
            const bool was = _keysWas[a_vk];
            _keysWas[a_vk] = now;
            return now && !was;
		};
		if (pressed(s.keyOverhead) && !_charging) {
			_overhead = !_overhead;
		}
		if (pressed(s.keyLeave) && tasks) {
			tasks->AddTask([this]() { EndRound("you left"); });
		}
	}

	// ---------------------------------------------------------------- main thread

	bool Golf::IsGamePaused()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		if (ui->freezeFramePause != 0 || (ui->menuMode != 0 && !menus::Open(menus::kDialogue))) {
			return true;
		}
		return menus::AnyBlocking();
	}

	void Golf::Tick()
	{
		const auto now = std::chrono::steady_clock::now();
		float      dt = _haveLast ? std::chrono::duration<float>(now - _last).count() : 0.0f;
		_last = now;
		_haveLast = true;
		dt = std::clamp(dt, 0.0f, kMaxFrame);
		if (IsGamePaused()) {
			return;  // (menus, the Pip-Boy, the workshop: everything holds still)
		}
		_clock += dt;
		// a round ended by a loading screen: its free camera switched off now the game's back
		if (_cameraOffAfterLoad && !menus::Open(menus::kLoading)) {
			_cameraOffAfterLoad = false;
			if (!_playing && InFreeCamera()) {
				if (auto* camera = RE::PlayerCamera::GetSingleton()) {
					camera->ToggleFreeCameraMode(false);
				}
			}
		}
		_scanTimer -= dt;
		if (_scanTimer <= 0.0f) {
			_scanTimer = kScanEvery;
			Scan();
		}
		if (_pendingCourse) {
			_pendingAge += dt;
			DecidePending();
		}
		if (_playing) {
			if (menus::Open(menus::kDialogue)) {
				EndRound("a conversation started");
			} else {
				TickPlay(dt);
			}
		}
		_bannerAge += dt;
		UpdateHud();
	}

	void Golf::Scan()
	{
		if (auto* map = RE::ControlMap::GetSingleton()) {
			// (keyboard bindings are Windows virtual-key codes: F = 0x46)
			const auto vk = map->GetMappedKey("Activate", RE::INPUT_DEVICE::kKeyboard);
			if (vk != RE::ControlMap::kInvalid && vk > 0 && vk < 0xFF) {
				_activateVk = static_cast<int>(vk);
			}
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Forms::Course() || !_loaded) {
			_placed.clear();
			return;
		}
		std::vector<RE::TESObjectREFR*> found;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, kScanRange, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && Forms::IsCourse(a_ref) && !a_ref->IsDisabled() && !a_ref->IsDeleted() && a_ref->Get3D() &&
					std::ranges::find(found, a_ref) == found.end()) {
					found.push_back(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		std::vector<Placed> next;
		for (auto* ref : found) {
			auto it = std::ranges::find_if(_placed, [&](const Placed& p) { return p.ref.get().get() == ref; });
			if (it != _placed.end()) {
				next.push_back(std::move(*it));
			} else {
				next.push_back({ ref->GetHandle(), nullptr, {} });
			}
		}
		_placed = std::move(next);
		// the first time you're near a course: how to play it (once a session)
		if (!_toldHowToPlay && !_playing) {
			for (auto* ref : found) {
				if ((ref->data.location - player->data.location).Length() < 1200.0f) {
					_toldHowToPlay = true;
					const int         act = _activateVk.load();
					const std::string key = act > 0 ? Settings::Key(act) : std::string("ACTIVATE");
					RE::SendHUDMessage::ShowHUDMessage(
						std::format("Mini Golf: {} on the course plays a round, hold {} to play a settler 1v1", key, key).c_str(), nullptr, false, false);
					break;
				}
			}
		}
		// the round's course gone (scrapped, unloaded): the round's over
		if (_playing && !_courseRef.get()) {
			EndRound("the course is gone");
		}
	}

	void Golf::PlayNearest()
	{
		if (_playing || IsGamePaused()) {
			return;
		}
		Scan();
		auto*              player = RE::PlayerCharacter::GetSingleton();
		RE::TESObjectREFR* best = nullptr;
		float              bestD = 2500.0f;  // (the course is about 18.5 m across: from anywhere on it, or just by it)
		for (auto& p : _placed) {
			auto ref = p.ref.get();
			if (!ref || !player) {
				continue;
			}
			const float d = (ref->data.location - player->data.location).Length();
			if (d < bestD) {
				bestD = d;
				best = ref.get();
			}
		}
		if (!best) {
			RE::SendHUDMessage::ShowHUDMessage("Mini Golf: no course near you", nullptr, false, false);
			return;
		}
		if (!_loaded) {
			Activated(best);  // (says the data's missing)
			return;
		}
		StartRound(best);  // (the play key: solo)
	}

	void Golf::Activated(RE::TESObjectREFR* a_ref)
	{
		if (_playing || IsGamePaused()) {
			return;
		}
		if (!_loaded) {
			RE::SendHUDMessage::ShowHUDMessage("Mini Golf: the course's data (course.txt) is missing; see the log", nullptr, false, false);
			return;
		}
		// a tap or a hold? (the game activates on the press: wait for the key)
		_pendingCourse = a_ref->GetHandle();
		_pendingAge = 0.0f;
		if (_activateVk.load() <= 0) {
			_pendingCourse = {};
			StartRound(a_ref);
		}
	}

	void Golf::DecidePending()
	{
		auto course = _pendingCourse.get();
		if (!course || _playing) {
			_pendingCourse = {};
			return;
		}
		const auto now = NowMs();
		const auto downAt = _activateDownAt.load();
		const auto upAt = _activateUpAt.load();
		bool       hold = false;
		if (_activateDown.load()) {
			if (now - downAt < kHoldMs && _pendingAge < 3.0f) {
				return;  // still down: not yet a hold
			}
			hold = now - downAt >= kHoldMs;
		} else {
			if (now - upAt > 250 && _pendingAge < 0.2f) {
				return;  // (the press may not have been seen yet)
			}
			hold = upAt > downAt && upAt - downAt >= kHoldMs && now - upAt < 1500;
		}
		_pendingCourse = {};
		RE::Actor* opponent = nullptr;
		if (hold) {
			opponent = FindOpponent(course.get());
			if (!opponent) {
				RE::SendHUDMessage::ShowHUDMessage("Mini Golf: nobody near enough to play you. A solo round, then", nullptr, false, false);
			}
		}
		StartRound(course.get(), opponent);
	}

	void Golf::StartRound(RE::TESObjectREFR* a_course, RE::Actor* a_opponent)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !FreeCamera()) {
			return;
		}
		Settings::Get().Load();
		_courseRef = a_course->GetHandle();
		_card.assign(_course.holes.size(), 0);
		_oppCard.clear();
		_oppTurn = false;
		_opponent = {};
		_opponentName.clear();
		_plan.reset();
		_strikeAt = -1.0;
		if (a_opponent) {
			_opponent = a_opponent->GetHandle();
			const char* name = a_opponent->GetDisplayFullName();
			_opponentName = name ? name : "";
			if (_opponentName.empty()) {
				_opponentName = "Settler";
			}
			_oppCard.assign(_course.holes.size(), 0);
			Papyrus::SetRestrained(a_opponent, true);
			log::info("1v1 against {} ({:08X})", _opponentName, a_opponent->GetFormID());
			RE::SendHUDMessage::ShowHUDMessage(std::format("Mini Golf: you against {}. You putt first on every hole", _opponentName).c_str(), nullptr, false, false);
		}
		_playing = true;
		EnterCamera();
		BlockControls(true);
		log::info("round started on the course {:08X}", a_course->GetFormID());
		StartHole(0);
	}

	void Golf::StartHole(int a_index)
	{
		const auto& hole = _course.holes[a_index];
		_hole = a_index;
		_sim = std::make_unique<golf::Sim>(hole, Settings::Get().spec);
		_sim->SetTime(_clock);
		_stroke = 0;
		_phase = Phase::kAim;
		_phaseTime = 0.0f;
		_lastRest = _sim->Pos();
		_charging = false;
		_overhead = false;
		_showCard = false;
		_oppTurn = false;
		_plan.reset();
		_strikeAt = -1.0;
		const double yaw = hole.teeYaw * kDegToRad;  // (clockwise from +y, in the course's own frame)
		_aimDir = { std::sin(yaw), std::cos(yaw) };
		Frame frame;
		if (auto course = _courseRef.get(); course && FrameOf(course.get(), frame)) {
			std::scoped_lock lock(_camMutex);
			_wantYaw = YawOf(frame, _aimDir);
			_turnTo = true;
			_haveCamTarget = false;
			// the view from above: over the middle of the hole, high enough to see all of it
			double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18, z1 = -1e18;
			for (const auto& t : hole.tris) {
				x0 = std::min(x0, t.minx);
				y0 = std::min(y0, t.miny);
				x1 = std::max(x1, t.maxx);
				y1 = std::max(y1, t.maxy);
				z1 = std::max({ z1, t.a.z, t.b.z, t.c.z });
			}
			_overheadAt = ToWorld(frame, { (x0 + x1) * 0.5, (y0 + y1) * 0.5, z1 });
			_overheadHeight = static_cast<float>(std::clamp(std::max(x1 - x0, y1 - y0) * 1.05, 160.0, 650.0));
		}
		MovePlayerToTee();
		if (Versus()) {
			OpponentWatching();
		}
		Banner(std::format("HOLE {}", hole.number), std::format("{} · PAR {}", Upper(hole.name), hole.par), true);
		log::info("hole {} ({}, par {})", hole.number, hole.name, hole.par);
	}

	void Golf::EndRound(const char* a_why)
	{
		if (!_playing) {
			return;
		}
		int total = 0, par = 0, played = 0;
		for (std::size_t i = 0; i < _card.size(); ++i) {
			if (_card[i] > 0) {
				total += _card[i];
				par += _course.holes[i].par;
				++played;
			}
		}
		log::info("round over ({}): {} strokes over {} holes (par {})", a_why, total, played, par);
		if (Versus() && played > 0) {
			int theirs = 0;
			for (std::size_t i = 0; i < _oppCard.size(); ++i) {
				theirs += _card[i] > 0 ? _oppCard[i] : 0;
			}
			const bool        finished = std::ranges::none_of(_oppCard, [](int s) { return s == 0; });
			const std::string verdict = total < theirs ? std::string("you win") : total > theirs ? std::format("{} wins", _opponentName) : std::string("all square");
			log::info("against {}: {} to {} ({})", _opponentName, total, theirs, finished ? "the whole round" : "so far");
			RE::SendHUDMessage::ShowHUDMessage(std::format("Mini Golf: you {}, {} {}{}", total, _opponentName, theirs,
												   finished ? std::format(": {}", verdict) : std::string(" (the round ended early)"))
												   .c_str(),
				nullptr, false, false);
		} else if (played > 0) {
			const int diff = total - par;
			RE::SendHUDMessage::ShowHUDMessage(std::format("Mini Golf: {} strokes over {} hole{} ({}{})", total, played, played == 1 ? "" : "s",
												   diff > 0 ? "+" : "", diff == 0 ? std::string("even") : std::to_string(diff))
												   .c_str(),
				nullptr, false, false);
		}
		_playing = false;
		_charging = false;
		_overhead = false;
		_phase = Phase::kAim;
		ReleaseOpponent();
		_opponent = {};
		_oppTurn = false;
		_plan.reset();
		_strikeAt = -1.0;
		LeaveCamera();
		BlockControls(false);
		RemoveObjects();
		_sim.reset();
		_courseRef = {};
		_showCard = false;
		UpdateHud();
	}

	void Golf::Abort(const char* a_why)
	{
		if (_playing) {
			EndRound(a_why);
		}
		_placed.clear();
		RemoveObjects();
	}

	void Golf::Putt(float a_power)
	{
		if (!_playing || !_sim || _phase != Phase::kAim || _sim->Moving() || IsGamePaused()) {
			return;
		}
		if (a_power < 0.02f) {
			return;  // (no power: no putt, no stroke)
		}
		if (_oppTurn) {
			return;
		}
		const auto& s = Settings::Get();
		const double speed = s.spec.maxSpeed * std::pow(static_cast<double>(std::clamp(a_power, 0.0f, 1.0f)), s.powerCurve);
		Strike(_aimDir, speed, 0.35f + 0.65f * a_power);
		log::info("putt {} on hole {}: power {:.0f}% ({:.0f} units/s) towards ({:.2f}, {:.2f})", _stroke, _course.holes[_hole].number,
			a_power * 100.0f, speed, _aimDir.x, _aimDir.y);
	}

	void Golf::Strike(golf::Vec2 a_dir, double a_speed, float a_loudness)
	{
		_lastRest = _sim->Pos();
		_sim->Strike(a_dir, a_speed);
		++_stroke;
		_phase = Phase::kRolling;
		_phaseTime = 0.0f;
		_showCard = false;
		Frame frame;
		if (auto course = _courseRef.get(); course && FrameOf(course.get(), frame)) {
			Audio::Play(Audio::Bank::kPutt, ToWorld(frame, _lastRest), a_loudness);
		}
	}

	void Golf::TickPlay(float a_dt)
	{
		if (!_sim || !_courseRef.get()) {
			return;
		}
		if (Versus() && Settings::Get().oppHidden) {
			ShowOpponent(false);  // (every tick: their 3D can be rebuilt)
		}
		_phaseTime += a_dt;
		_shotSoundGap = std::max(0.0f, _shotSoundGap - a_dt);
		// a settler's putt due this frame: the ball's world stepped to that very moment and the putt played there, as
		// it was planned (a frame late, a moving obstacle is somewhere else)
		if (_oppTurn && _phase == Phase::kAim && _strikeAt >= 0.0 && _strikeAt <= _clock && !_sim->Moving()) {
			if (_strikeAt > _sim->Time()) {
				ShotEvents(_sim->Step(_strikeAt - _sim->Time()));
			}
			if (!_sim->Moving()) {
				_strikeAt = -1.0;
				const float loud = static_cast<float>(std::clamp(_oppShot.speed / Settings::Get().spec.maxSpeed, 0.0, 1.0));
				Strike(_oppShot.dir, _oppShot.speed, 0.35f + 0.65f * loud);
				log::info("{}: putt {} on hole {}", _opponentName, _stroke, _course.holes[_hole].number);
			}
		}
		// the ball's clock keeps up with the obstacles' (they go by _clock, the ball's world by the sim's own)
		const double behind = _clock - _sim->Time();
		if (behind > 0.0) {
			ShotEvents(_sim->Step(std::min(behind, 0.25)));
			if (_clock - _sim->Time() > 0.0) {
				_sim->SetTime(_clock);
			}
		}
		const auto& s = Settings::Get();
		switch (_phase.load()) {
		case Phase::kAim:
			if (_sim->Moving()) {
				_phase = Phase::kRolling;  // knocked by an obstacle while you aimed: watch it go (no stroke)
				_phaseTime = 0.0f;
				_charging = false;
				_plan.reset();  // (theirs: planned for where it was)
				_strikeAt = -1.0;
			} else if (_oppTurn) {
				TickOpponent();
			}
			break;
		case Phase::kRolling:
			switch (_sim->State()) {
			case golf::BallState::kRest:
				if (_stroke >= s.maxStrokes) {
					FinishHole(false);
				} else {
					_phase = Phase::kAim;
				}
				break;
			case golf::BallState::kInCup:
				FinishHole(true);
				break;
			case golf::BallState::kWater:
			case golf::BallState::kOut:
				++_stroke;
				Banner(_sim->State() == golf::BallState::kWater ? "WATER" : "OUT OF BOUNDS", "+1 STROKE", false);
				_phase = Phase::kPenalty;
				_phaseTime = 0.0f;
				break;
			default:
				break;
			}
			break;
		case Phase::kPenalty:
			if (_phaseTime > kPenaltySeconds) {
				_sim->Place(_lastRest.XY(), _lastRest.z + 1.0);
				if (_stroke >= s.maxStrokes) {
					FinishHole(false);
				} else {
					_phase = Phase::kAim;
				}
			}
			break;
		case Phase::kHoled:
			if (_phaseTime > kHoledSeconds) {
				if (Versus() && !_oppTurn) {
					StartOpponentTurn();  // your ball's in: their go at the hole
				} else if (_hole + 1 < static_cast<int>(_course.holes.size())) {
					StartHole(_hole + 1);
				} else {
					int total = 0, par = 0;
					for (std::size_t i = 0; i < _card.size(); ++i) {
						total += _card[i];
						par += _course.holes[i].par;
					}
					const int diff = total - par;
					if (Versus()) {
						int theirs = 0;
						for (const int s : _oppCard) {
							theirs += s;
						}
						const std::string name = Upper(_opponentName);
						Banner(total < theirs ? std::string("YOU WIN!") : total > theirs ? std::format("{} WINS", name) : std::string("ALL SQUARE"),
							std::format("YOU {} · {} {}", total, name, theirs), total <= theirs);
						if (total < theirs) {
							Audio::PlayUI(Audio::Bank::kHoleIn, 0.8f);
						}
					} else {
						Banner("ROUND OVER", std::format("{} STROKES · {}", total, diff == 0 ? std::string("EVEN PAR") : std::format("{}{}", diff > 0 ? "+" : "", diff)),
							diff <= 0);
					}
					_phase = Phase::kDone;
					_phaseTime = 0.0f;
					_showCard = true;
				}
			}
			break;
		case Phase::kDone:
			if (_phaseTime > kDoneSeconds) {
				EndRound("the round's over");
			}
			break;
		}
	}

	void Golf::ShotEvents(const std::vector<golf::Event>& a_events)
	{
		Frame frame;
		auto  course = _courseRef.get();
		if (a_events.empty() || !course || !FrameOf(course.get(), frame)) {
			return;
		}
		for (const auto& e : a_events) {
			const auto at = ToWorld(frame, e.pos);
			switch (e.type) {
			case golf::Event::Type::kWall:
				if (_shotSoundGap <= 0.0f) {
					Audio::Play(Audio::Bank::kWall, at, static_cast<float>(std::clamp(e.speed / 250.0, 0.08, 1.0)));
					_shotSoundGap = 0.04f;
				}
				break;
			case golf::Event::Type::kRim:
				Audio::Play(Audio::Bank::kRim, at, static_cast<float>(std::clamp(e.speed / 150.0, 0.15, 1.0)));
				break;
			case golf::Event::Type::kLand:
				Audio::Play(Audio::Bank::kLand, at, static_cast<float>(std::clamp(e.speed / 200.0, 0.1, 1.0)));
				break;
			case golf::Event::Type::kCup:
				Audio::Play(Audio::Bank::kCup, at, 1.0f);
				break;
			case golf::Event::Type::kWater:
				Audio::Play(Audio::Bank::kSplash, at, 0.9f);
				break;
			default:
				break;
			}
		}
	}

	void Golf::FinishHole(bool a_holed)
	{
		const auto& hole = _course.holes[_hole];
		const int   max = Settings::Get().maxStrokes;
		const int   score = a_holed ? _stroke : max;
		ActiveCard()[_hole] = score;
		const int diff = score - hole.par;
		std::string title;
		if (!a_holed) {
			title = "PICKED UP";
		} else if (score == 1) {
			title = "HOLE IN ONE!";
		} else if (diff <= -3) {
			title = "ALBATROSS!";
		} else if (diff == -2) {
			title = "EAGLE!";
		} else if (diff == -1) {
			title = "BIRDIE!";
		} else if (diff == 0) {
			title = "PAR";
		} else if (diff == 1) {
			title = "BOGEY";
		} else if (diff == 2) {
			title = "DOUBLE BOGEY";
		} else {
			title = std::format("+{}", diff);
		}
		std::string sub = a_holed ? std::format("{} STROKE{}", score, score == 1 ? "" : "S") : std::format("{} STROKES: THE MOST A HOLE TAKES", max);
		if (_oppTurn) {
			sub = std::format("{} · {}", Upper(_opponentName), sub);
		}
		Banner(title, sub, a_holed && diff <= 0);
		if (a_holed) {
			Audio::PlayUI(Audio::Bank::kHoleIn, (score == 1 ? 1.0f : diff < 0 ? 0.7f : 0.4f) * (_oppTurn ? 0.6f : 1.0f));
		}
		log::info("hole {}: {}{} ({} strokes, par {})", hole.number, _oppTurn ? _opponentName + ": " : std::string(), title, score, hole.par);
		_phase = Phase::kHoled;
		_phaseTime = 0.0f;
		_showCard = true;
	}

	void Golf::Banner(std::string a_title, std::string a_sub, bool a_good)
	{
		_banner = std::move(a_title);
		_bannerSub = std::move(a_sub);
		_bannerGood = a_good;
		_bannerAge = 0.0f;
	}

	void Golf::MovePlayerToTee()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto  course = _courseRef.get();
		Frame frame;
		if (!player || !_sim || !course || !FrameOf(course.get(), frame)) {
			return;
		}
		// beside the ball, a little back, facing it (a putting stance): the ball off your left foot
		const auto&      ball = _sim->Pos();
		const golf::Vec2 side{ -_aimDir.y, _aimDir.x };  // to the left of the aim
		const golf::Vec3 stand{ ball.x + side.x * 16.0 - _aimDir.x * 4.0, ball.y + side.y * 16.0 - _aimDir.y * 4.0, ball.z - Settings::Get().spec.ballRadius };
		const auto       at = ToWorld(frame, stand);
		const auto       look = ToWorld(frame, ball) - at;
		// moved directly, not with Papyrus' SetPosition: on the player that is a teleport (a loading screen, and the free
		// camera the round is played in doesn't come back from it: a black screen)
		player->SetPosition(at, true);
		player->data.angle.z = std::atan2(look.x, look.y);
	}

	// ---------------------------------------------------------------- the settler you're playing

	RE::Actor* Golf::Opponent() const
	{
		auto  ref = _opponent.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor || actor->IsDead(false) || actor->IsDisabled() || actor->IsDeleted()) {
			return nullptr;
		}
		return actor;
	}

	RE::Actor* Golf::FindOpponent(RE::TESObjectREFR* a_course) const
	{
		// the nearest grown-up, friendly person to you (settlers, companions), not fighting, not in power armour
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_course) {
			return nullptr;
		}
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x00013794);  // ActorTypeNPC
		const float  within = Settings::Get().oppRange;
		RE::Actor*   best = nullptr;
		float        bestD = within;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, within, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!actor || actor == player || actor->IsDisabled() || actor->IsDead(false) || !actor->Get3D() || actor->IsChild() ||
					actor->IsInCombat() || actor->GetHostileToActor(player) || util::InPowerArmor(actor) ||
					(npcType && !actor->HasKeywordHelper(npcType, nullptr))) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float d = (actor->data.location - player->data.location).Length();
				if (d < bestD) {
					bestD = d;
					best = actor;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		return best;
	}

	void Golf::StartOpponentTurn()
	{
		const auto& hole = _course.holes[_hole];
		_sim = std::make_unique<golf::Sim>(hole, Settings::Get().spec);
		_sim->SetTime(_clock);
		_stroke = 0;
		_phase = Phase::kAim;
		_phaseTime = 0.0f;
		_lastRest = _sim->Pos();
		_charging = false;
		_overhead = false;
		_showCard = false;
		_oppTurn = true;
		_plan.reset();
		_strikeAt = -1.0;
		_planTries = 0;
		const double yaw = hole.teeYaw * kDegToRad;
		_aimDir = { std::sin(yaw), std::cos(yaw) };
		Frame frame;
		if (auto course = _courseRef.get(); course && FrameOf(course.get(), frame)) {
			std::scoped_lock lock(_camMutex);
			_wantYaw = YawOf(frame, _aimDir);
			_turnTo = true;
			_haveCamTarget = false;
		}
		PlayerWatching();
		OpponentAtBall();
		Banner(std::format("{}'S TURN", Upper(_opponentName)), std::format("HOLE {} · PAR {}", hole.number, hole.par), true);
		log::info("{} plays hole {}", _opponentName, hole.number);
	}

	void Golf::TickOpponent()
	{
		auto* actor = Opponent();
		if (!actor) {
			DropOpponent("they're gone");
			return;
		}
		if (actor->IsInCombat()) {
			DropOpponent("they're in a fight");
			return;
		}
		if (_strikeAt < 0.0) {
			if (!_plan) {
				PlanOpponentShot();
				return;
			}
			if (!_plan->done.load()) {
				return;  // (still working it out)
			}
			golf::Shot shot = _plan->shot;
			_plan.reset();
			const bool   moving = !_course.holes[_hole].obstacles.empty();
			const double at = _planFrom + _planLead + shot.wait;
			if (at < _clock - 0.05 && moving && _planTries < 5) {
				++_planTries;  // worked out too slowly: the obstacles have moved on. Again, further ahead
				PlanOpponentShot();
				return;
			}
			_planTries = 0;
			_oppShot = shot;  // (already off a little by their skill: Bot::Settler)
			_strikeAt = std::max(at, _clock + 0.6);
			_aimDir = shot.dir;
			Frame frame;
			if (auto course = _courseRef.get(); course && FrameOf(course.get(), frame)) {
				std::scoped_lock lock(_camMutex);
				_wantYaw = YawOf(frame, _aimDir);  // the view turned down their line
				_turnTo = true;
			}
			OpponentAtBall();
			log::info("{}: putt {} planned at {:.0f} units/s (in {:.1f} s)", _opponentName, _stroke + 1, shot.speed, _strikeAt - _clock);
			return;
		}
		// (the putt itself: TickPlay, at the very moment)
	}

	void Golf::PlanOpponentShot()
	{
		// the physics' player (Bot) tries a few hundred shots on a copy of the ball's world as it will be a moment from
		// now, on a worker thread (it takes a second or two); the putt goes when that moment comes
		auto plan = std::make_shared<Plan>();
		_plan = plan;
		const golf::Hole* hole = &_course.holes[_hole];
		const bool        moving = !hole->obstacles.empty();
		_planFrom = _sim->Time();  // (with moving obstacles, late means a mistimed putt: worked out again, further ahead)
		_planLead = (moving ? 3.0 : 1.8) + 2.5 * _planTries;
		const double        lead = _planLead;
		const golf::Spec    spec = Settings::Get().spec;
		const double        skill = Settings::Get().oppSkill;
		const std::uint32_t seed = _planSeed++ ^ static_cast<std::uint32_t>(NowMs());
		golf::Sim           copy = *_sim;
		std::thread([plan, copy = std::move(copy), hole, spec, lead, seed, skill]() mutable {
			golf::Bot bot(*hole, spec);
			try {
				copy.Step(lead);
				const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 2, 1, 12);
				plan->shot = bot.Settler(copy, skill, seed, threads);
			} catch (...) {
				plan->shot = {};
			}
			if (plan->shot.speed <= 0.0) {
				plan->shot.dir = bot.Toward(copy.Pos());  // (the cup's way, at a firm putt)
				plan->shot.speed = spec.maxSpeed * 0.35;
				plan->shot.wait = 0.0;
			}
			plan->done = true;
		}).detach();
	}

	void Golf::PlaceOpponent(const golf::Vec3& a_stand, const golf::Vec3& a_look)
	{
		if (Settings::Get().oppHidden) {
			return;  // (invisible: they stay where they were)
		}
		auto* actor = Opponent();
		auto  course = _courseRef.get();
		Frame frame;
		if (!actor || !course || !FrameOf(course.get(), frame)) {
			return;
		}
		const auto  at = ToWorld(frame, a_stand);
		const auto  look = ToWorld(frame, a_look) - at;
		const float yaw = std::atan2(look.x, look.y);
		Papyrus::SetPosition(actor, at);
		Papyrus::SetAngle(actor, 0.0f, 0.0f, yaw / kDegToRad);
	}

	void Golf::OpponentAtBall()
	{
		if (!_sim) {
			return;
		}
		// as you stand: beside the ball, a little back, facing it
		const auto&      ball = _sim->Pos();
		const golf::Vec2 side{ -_aimDir.y, _aimDir.x };
		const double     r = Settings::Get().spec.ballRadius;
		PlaceOpponent({ ball.x + side.x * 16.0 - _aimDir.x * 4.0, ball.y + side.y * 16.0 - _aimDir.y * 4.0, ball.z - r }, ball);
	}

	void Golf::OpponentWatching()
	{
		if (!_sim) {
			return;
		}
		// the other side of the tee from you, back a bit, watching
		const auto&      ball = _sim->Pos();
		const golf::Vec2 side{ -_aimDir.y, _aimDir.x };
		const double     r = Settings::Get().spec.ballRadius;
		PlaceOpponent({ ball.x - side.x * 26.0 - _aimDir.x * 16.0, ball.y - side.y * 26.0 - _aimDir.y * 16.0, ball.z - r }, ball);
	}

	void Golf::PlayerWatching()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto  course = _courseRef.get();
		Frame frame;
		if (!player || !_sim || !course || !FrameOf(course.get(), frame)) {
			return;
		}
		const auto&      ball = _sim->Pos();
		const golf::Vec2 side{ -_aimDir.y, _aimDir.x };
		const golf::Vec3 stand{ ball.x - side.x * 26.0 - _aimDir.x * 16.0, ball.y - side.y * 26.0 - _aimDir.y * 16.0, ball.z - Settings::Get().spec.ballRadius };
		const auto       at = ToWorld(frame, stand);
		const auto       look = ToWorld(frame, ball) - at;
		player->SetPosition(at, true);  // (not Papyrus': see MovePlayerToTee)
		player->data.angle.z = std::atan2(look.x, look.y);
	}

	void Golf::ShowOpponent(bool a_show)
	{
		auto* actor = Opponent();
		auto* root = actor ? actor->Get3D() : nullptr;
		if (root && root->GetAppCulled() == a_show) {
			root->SetAppCulled(!a_show);
		}
	}

	void Golf::ReleaseOpponent()
	{
		ShowOpponent(true);
		if (auto* actor = Opponent()) {
			Papyrus::SetRestrained(actor, false);
			Papyrus::EvaluatePackage(actor);
			log::info("{}: let go", _opponentName);
		}
	}

	void Golf::DropOpponent(const char* a_why)
	{
		log::info("{} stopped playing: {}", _opponentName, a_why);
		RE::SendHUDMessage::ShowHUDMessage(std::format("Mini Golf: {} stopped playing. You play on alone", _opponentName).c_str(), nullptr, false, false);
		ReleaseOpponent();
		_opponent = {};
		_plan.reset();
		_strikeAt = -1.0;
		if (_oppTurn) {
			_oppTurn = false;
			_phase = Phase::kHoled;  // on to the next hole
			_phaseTime = kHoledSeconds;
		}
	}

	// ---------------------------------------------------------------- the course in the world

	bool Golf::FrameOf(RE::TESObjectREFR* a_course, Frame& a_out) const
	{
		auto* root = a_course ? a_course->Get3D() : nullptr;
		if (!root) {
			return false;
		}
		const auto& w = root->GetWorldTransform();
		a_out.rot = w.rotate;
		a_out.origin = w.translate;
		a_out.scale = w.scale != 0.0f ? w.scale : 1.0f;
		return true;
	}

	RE::NiPoint3 Golf::ToWorld(const Frame& a_frame, const golf::Vec3& a_p) const
	{
		const RE::NiPoint3 local{ static_cast<float>(a_p.x), static_cast<float>(a_p.y), static_cast<float>(a_p.z) };
		return a_frame.origin + a_frame.rot.Transpose() * (local * a_frame.scale);
	}

	golf::Vec2 Golf::DirToCourse(const Frame& a_frame, float a_worldYaw) const
	{
		const auto c = a_frame.rot * RE::NiPoint3{ std::sin(a_worldYaw), std::cos(a_worldYaw), 0.0f };
		const double l = std::hypot(c.x, c.y);
		return l > 1e-6 ? golf::Vec2{ c.x / l, c.y / l } : golf::Vec2{ 1.0, 0.0 };
	}

	float Golf::YawOf(const Frame& a_frame, const golf::Vec2& a_dir) const
	{
		const auto w = a_frame.rot.Transpose() * RE::NiPoint3{ static_cast<float>(a_dir.x), static_cast<float>(a_dir.y), 0.0f };
		return std::atan2(w.x, w.y);
	}

	// ---------------------------------------------------------------- every frame (camera update)

	void Golf::Animate()
	{
		if (!_loaded || _course.holes.empty()) {
			return;
		}
		RE::NiUpdateData update;
		for (auto& p : _placed) {
			auto ref = p.ref.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root) {
				p.root = nullptr;
				continue;
			}
			if (root != p.root) {
				// a new 3D (loaded, rebuilt): its obstacles' nodes, by name
				p.root = root;
				p.nodes.clear();
				int missing = 0;
				for (const auto& h : _course.holes) {
					for (const auto& o : h.obstacles) {
						auto* node = root->GetObjectByName(RE::BSFixedString(o.name.c_str()));
						p.nodes.push_back(node);
						missing += node == nullptr;
					}
				}
				if (missing) {
					log::warn("course {:08X}: {} obstacle node(s) not found in its model", ref->GetFormID(), missing);
				}
			}
			std::size_t k = 0;
			for (const auto& h : _course.holes) {
				for (const auto& o : h.obstacles) {
					auto* node = p.nodes[k++];
					if (!node) {
						continue;
					}
					double     angle = 0.0, lift = 0.0;
					golf::Vec2 offset;
					o.Pose(_clock, angle, offset, lift);
					const M3   rest = RotZ(o.yawDegrees * kDegToRad);
					M3         m = rest;
					RE::NiPoint3 at{ static_cast<float>(o.origin.x), static_cast<float>(o.origin.y), static_cast<float>(o.origin.z) };
					switch (o.motion) {
					case golf::Obstacle::Motion::kSpin:
					case golf::Obstacle::Motion::kSwing:
						m = Mul(RotZ(angle), rest);
						break;
					case golf::Obstacle::Motion::kWheel:
					case golf::Obstacle::Motion::kPendulum:
						{
							const double a = o.axisDegrees * kDegToRad;
							m = Mul(AxisAngle(std::cos(a), std::sin(a), 0.0, angle), rest);
						}
						break;
					case golf::Obstacle::Motion::kSlide:
						at.x += static_cast<float>(offset.x);
						at.y += static_cast<float>(offset.y);
						break;
					case golf::Obstacle::Motion::kLift:
						at.z += static_cast<float>(lift);
						break;
					}
					node->local.rotate = Ni(m);
					node->local.translate = at;
					node->Update(update);
				}
			}
		}
		if (_playing && !_saving) {
			PlaceObjects();
		}
	}

	RE::ObjectRefHandle Golf::Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto  course = _courseRef.get();
		auto* cell = course ? course->GetParentCell() : nullptr;
		if (!cell) {
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				cell = player->GetParentCell();
			}
		}
		if (!dh || !cell || !a_form) {
			return {};
		}
		RE::NEW_REFR_DATA data;
		data.location = a_pos;
		data.direction = { 0.0f, 0.0f, 0.0f };
		data.object = a_form;
		if (cell->IsInterior()) {
			data.interior = cell;
		} else {
			data.world = cell->worldSpace;
		}
		data.initializeScripts = false;
		return dh->CreateReferenceAtLocation(data);
	}

	void Golf::Remove(RE::ObjectRefHandle& a_handle)
	{
		if (auto ref = a_handle.get()) {
			ref->Disable();
			ref->SetWantsDelete(true);
		}
		a_handle = {};
	}

	void Golf::Place(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, bool a_hidden)
	{
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root) {
			return;
		}
		if (root->GetAppCulled() != a_hidden) {
			root->SetAppCulled(a_hidden);
		}
		if (a_hidden) {
			return;
		}
		RE::NiTransform local;
		if (auto* parent = root->parent) {
			const auto& p = parent->GetWorldTransform();
			const float ps = p.scale != 0.0f ? p.scale : 1.0f;
			local.rotate = a_rot * p.rotate.Transpose();
			local.translate = (p.rotate * (a_pos - p.translate)) * (1.0f / ps);
			local.scale = 1.0f / ps;
		} else {
			local.rotate = a_rot;
			local.translate = a_pos;
			local.scale = 1.0f;
		}
		root->SetLocalTransform(local);
		RE::NiUpdateData update;
		root->Update(update);
		a_ref->data.location.x = a_pos.x;
		a_ref->data.location.y = a_pos.y;
		a_ref->data.location.z = a_pos.z;
	}

	void Golf::PlaceObjects()
	{
		auto  course = _courseRef.get();
		Frame frame;
		if (!_sim || !course || !FrameOf(course.get(), frame)) {
			return;
		}
		const auto& s = Settings::Get();
		const double r = s.spec.ballRadius;

		// the ball: where the physics has it, turned as it's rolled
		const auto ballAt = ToWorld(frame, _sim->Pos());
		if (!_ball) {
			_ball = Spawn(Forms::Ball(), ballAt);
		}
		if (auto ball = _ball.get()) {
			double q[3][3];
			_sim->Rot().ToMatrix(q);
			M3 m{};
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					m[i][j] = q[i][j];
				}
			}
			Place(ball.get(), ballAt, Ni(m) * frame.rot, _phase == Phase::kPenalty);
		}

		// (the aim line is drawn on the HUD, as Bowling's is: UpdateGuide)
		for (auto& h : _dots) {
			Remove(h);
		}
		_dots.clear();
	}

	void Golf::RemoveObjects()
	{
		Remove(_ball);
		for (auto& h : _dots) {
			Remove(h);
		}
		_dots.clear();
	}

	void Golf::BeforeSave()
	{
		_saving = true;
		RemoveObjects();  // nothing of ours in the save (they come back next frame)
	}

	void Golf::AfterSave()
	{
		_saving = false;
	}

	// ---------------------------------------------------------------- controls and the camera

	void Golf::BlockControls(bool a_block)
	{
		if (a_block == _controlsBlocked) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_inputLayer && !manager->AllocateNewLayer(_inputLayer, "fo4minigolf playing")) {
			log::warn("could not get an input layer; controls stay on while playing");
			return;
		}
		// mouse look stays on (it aims); walking, fighting, activating, the Pip-Boy and VATS are off
		using Flag = RE::UserEvents::USER_EVENT_FLAG;
		const auto flags = static_cast<Flag>(
			std::to_underlying(Flag::kMovement) | std::to_underlying(Flag::kActivate) | std::to_underlying(Flag::kPOVSwitch) |
			std::to_underlying(Flag::kFighting) | std::to_underlying(Flag::kSneaking) | std::to_underlying(Flag::kMainFour) |
			std::to_underlying(Flag::kWheelZoom) | std::to_underlying(Flag::kJumping) | std::to_underlying(Flag::kVATS));
		manager->EnableUserEvent(_inputLayer->layerID, flags, !a_block, RE::UserEvents::SENDER_ID::kGameplay);
		_controlsBlocked = a_block;
		log::info("player controls {}", a_block ? "blocked while playing" : "restored");
	}

	bool Golf::SwallowButton(const RE::ButtonEvent* a_event)
	{
		if (!_playing || !a_event) {
			return false;
		}
		// our keys (as the keyboard's virtual-key codes): they do nothing else while you play
		if (a_event->device.get() != RE::INPUT_DEVICE::kKeyboard) {
			return false;
		}
		const auto& s = Settings::Get();
		const auto  code = static_cast<int>(a_event->idCode);
		return code == s.keyLeave || code == s.keyOverhead || code == s.keyPlay;
	}

	void Golf::EnterCamera()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera) {
			return;
		}
		if (!InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		_entryYaw = player->data.angle.z;
		_entryPitch = player->data.angle.x;
		if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
			_entryFreeRotation = third->freeRotation;
		}
		_haveEntryView = true;
		std::scoped_lock lock(_camMutex);
		_camMapped = false;
		_mapFrames = 0;
		_haveCamTarget = false;
		_inCamera = true;
	}

	void Golf::LeaveCamera()
	{
		if (!_inCamera) {
			return;
		}
		_inCamera = false;
		Audio::SetListenerYaw(0.0f, false);
		HoldPlayerView();
		_haveEntryView = false;
		if (menus::Open(menus::kLoading)) {
			_cameraOffAfterLoad = true;  // (switching the free camera off under a loading screen leaves the view black)
			return;
		}
		if (InFreeCamera()) {
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				camera->ToggleFreeCameraMode(false);
			}
		}
	}

	void Golf::HoldPlayerView()
	{
		if (!_haveEntryView) {
			return;
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			player->data.angle.x = _entryPitch;
			player->data.angle.z = _entryYaw;
		}
		if (auto* camera = RE::PlayerCamera::GetSingleton()) {
			if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
				third->freeRotation = _entryFreeRotation;
			}
		}
	}

	bool Golf::OnLookMouse(const RE::MouseMoveEvent* a_event)
	{
		if (!_inCamera || !a_event) {
			return false;
		}
		if (!_charging) {
			return _overhead.load();  // (from above, the mouse doesn't turn the view: it's held looking down)
		}
		// the same event can reach both handlers: count it once
		std::scoped_lock lock(_camMutex);
		if (!(a_event == _lastMouseEvent && a_event->timeCode == _lastMouseTime)) {
			const auto& s = Settings::Get();
			const float counts = static_cast<float>(a_event->mouseInputY) * (s.invertPull ? -1.0f : 1.0f);
			_power = std::clamp(_power.load() + counts * s.pullPerCount, 0.0f, 1.0f);
		}
		_lastMouseEvent = a_event;
		_lastMouseTime = a_event->timeCode;
		return true;  // charging: the pull is power, the view holds still
	}

	void Golf::BeforeFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if (!_inCamera || !(_charging || _overhead) || !a_free) {
			return;
		}
		if (auto* camera = a_free->camera) {
			camera->rotationInput.x = 0.0f;
			camera->rotationInput.y = 0.0f;
		}
		if (auto* controls = RE::PlayerControls::GetSingleton()) {
			auto& d = controls->data;
			d.lookInputVec = {};
			d.lookInputVecNormalized = {};
			d.prevLookVec = {};
			d.rotationSpeedSpring.position = {};
			d.rotationSpeedSpring.velocity = {};
		}
	}

	void Golf::OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before)
	{
		if (!_inCamera || !a_free) {
			return;
		}
		std::scoped_lock lock(_camMutex);
		if (!_camMapped) {
			return;
		}
		if (_charging) {
			a_free->rotation = a_before;  // the view holds still while you pull back
			return;
		}
		const auto& s = Settings::Get();
		const int   pitchIndex = 1 - _yawIndex;
		const float pitch = _pitchSign * Component(a_free, pitchIndex);
		const float wanted = _overhead ? kOverheadPitch : std::clamp(pitch, s.camMinPitch * kDegToRad, s.camMaxPitch * kDegToRad);
		if (wanted != pitch) {
			Component(a_free, pitchIndex) = _pitchSign * wanted;
		}
	}

	bool Golf::FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out)
	{
		if (!_inCamera || !a_free) {
			return false;
		}
		std::scoped_lock lock(_camMutex);
		if (!_camMapped || !_haveCamTarget) {
			return false;
		}
		if (_overhead) {
			a_out = _overheadAt + RE::NiPoint3{ 0.0f, 0.0f, _overheadHeight };
			return true;
		}
		const float yaw = Component(const_cast<RE::FreeCameraState*>(a_free), _yawIndex);
		const float pitch = _pitchSign * Component(const_cast<RE::FreeCameraState*>(a_free), 1 - _yawIndex);
		const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		a_out = _camTarget - forward * Settings::Get().camDistance;
		return true;
	}

	void Golf::OnFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if (!_inCamera || !a_free) {
			return;
		}
		// (the camera's update: the main thread, as Tick is)
		if (!InFreeCamera()) {
			// something else took the camera (a scene, a menu that ends tfc): the round's over
			if (auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask([this]() { EndRound("the camera was taken"); });
			}
			return;
		}
		const auto& s = Settings::Get();
		Frame       frame;
		auto        course = _courseRef.get();
		const bool  haveFrame = _sim && course && FrameOf(course.get(), frame);
		{
			std::scoped_lock lock(_camMutex);
			// the free camera keeps two angles: which one is the yaw, worked out once against where you faced (the
			// free camera starts there). From Playable Pool.
			if (!_camMapped) {
				const float a = a_free->rotation.x;
				const float b = a_free->rotation.y;
				if (a == 0.0f && b == 0.0f && ++_mapFrames < 10) {
					return;
				}
				const float errA = std::abs(util::AngleDiff(a, _entryYaw));
				const float errB = std::abs(util::AngleDiff(b, _entryYaw));
				_yawIndex = errA <= errB ? 0 : 1;
				const float pitchValue = _yawIndex == 0 ? b : a;
				_pitchSign = 1.0f;
				if (std::abs(_entryPitch) > 0.05f && std::abs(pitchValue) > 0.05f && (pitchValue > 0.0f) != (_entryPitch > 0.0f)) {
					_pitchSign = -1.0f;
				}
				_camMapped = true;
				log::info("free camera: yaw is value {}, pitch sign {}", _yawIndex, _pitchSign);
			}
			if (_turnTo) {
				// a new hole: looking down it from behind the tee
				Component(a_free, _yawIndex) = _wantYaw;
				Component(a_free, 1 - _yawIndex) = _pitchSign * s.camPitch * kDegToRad;
				_turnTo = false;
			}
			if (_overhead) {
				Component(a_free, 1 - _yawIndex) = _pitchSign * kOverheadPitch;
			}

			// the point the camera orbits: the ball, followed smoothly
			const auto  now = std::chrono::steady_clock::now();
			const float dt = std::min(0.1f, std::chrono::duration<float>(now - _lastCam).count());
			_lastCam = now;
			if (haveFrame) {
				_camDesired = ToWorld(frame, _sim->Pos());
				if (!_haveCamTarget) {
					_camTarget = _camDesired;
					_haveCamTarget = true;
				} else {
					_camTarget = _camTarget + (_camDesired - _camTarget) * std::min(1.0f, dt * 6.0f);
				}
			}
			_camYaw = Component(a_free, _yawIndex);
			Audio::SetListenerYaw(_camYaw, true);
			if (_haveCamTarget) {
				RE::NiPoint3 pos;
				if (_overhead) {
					pos = _overheadAt + RE::NiPoint3{ 0.0f, 0.0f, _overheadHeight };
				} else {
					const float pitch = _pitchSign * Component(a_free, 1 - _yawIndex);
					const RE::NiPoint3 forward{ std::sin(_camYaw) * std::cos(pitch), std::cos(_camYaw) * std::cos(pitch), -std::sin(pitch) };
					pos = _camTarget - forward * s.camDistance;
				}
				a_free->translation = pos;
			}
		}
		// the aim is where you look (not while charging: it's held)
		if (haveFrame && _phase == Phase::kAim && !_charging && !_overhead && !_oppTurn) {
			_aimDir = DirToCourse(frame, _camYaw);
		}
		// the mouse that turns the view also reaches your own look controls: undone every frame
		HoldPlayerView();
	}

	// ---------------------------------------------------------------- the HUD

	void Golf::UpdateHud()
	{
		HudState h;
		h.playing = _playing;
		if (_playing && _hole < static_cast<int>(_course.holes.size())) {
			const auto& hole = _course.holes[_hole];
			h.hole = hole.number;
			h.name = Upper(hole.name);
			h.par = hole.par;
			h.stroke = _stroke;
			h.card = _card;
			for (std::size_t i = 0; i < _card.size(); ++i) {
				h.pars.push_back(_course.holes[i].par);
				if (_card[i] > 0) {
					h.total += _card[i];
					h.parSoFar += _course.holes[i].par;
				}
			}
			h.power = _charging ? _power.load() : -1.0f;
			h.overhead = _overhead;
			h.banner = _banner;
			h.bannerSub = _bannerSub;
			h.bannerAge = _bannerAge;
			h.good = _bannerGood;
			h.showCard = _showCard;
			h.finished = _phase == Phase::kDone;
			const auto& s = Settings::Get();
			UpdateGuide();
			h.guide = _guide;
			if (_oppTurn) {
				h.keys = std::format("{} IS PUTTING · RIGHT BUTTON / {}: VIEW FROM ABOVE · {}: END THE ROUND", Upper(_opponentName),
					Settings::Key(s.keyOverhead), Settings::Key(s.keyLeave));
			} else {
				h.keys = std::format("LEFT BUTTON: HOLD, PULL BACK, LET GO TO PUTT · RIGHT BUTTON / {}: VIEW FROM ABOVE · {}: END THE ROUND",
					Settings::Key(s.keyOverhead), Settings::Key(s.keyLeave));
			}
			if (Versus()) {
				h.opponent = Upper(_opponentName);
				h.oppCard = _oppCard;
				h.oppTurn = _oppTurn;
				for (std::size_t i = 0; i < _oppCard.size(); ++i) {
					if (_oppCard[i] > 0) {
						h.oppTotal += _oppCard[i];
						h.oppParSoFar += _course.holes[i].par;
					}
				}
			}
		}
		std::scoped_lock lock(_hudMutex);
		_hud = std::move(h);
	}

	void Golf::UpdateGuide()
	{
		// the green line, as Bowling's: from the ball's edge straight along your aim, a fixed length, lying on the
		// green (it follows the slopes), as screen points 0..1 for the HUD to draw. It ends at the green's edge.
		// It shows the line you're starting the ball on, not where the slopes take it.
		_guide.clear();
		const auto& s = Settings::Get();
		auto*       camera = RE::Main::WorldRootCamera();
		auto        course = _courseRef.get();
		Frame       frame;
		if (!s.guide || !_playing || _phase != Phase::kAim || _overhead || !_sim || _sim->Moving() || !camera || !course || (_oppTurn && _strikeAt < 0.0) ||
			!FrameOf(course.get(), frame)) {
			return;
		}
		const auto project = [&](const RE::NiPoint3& a_world, float& a_x, float& a_y) {
			float z = 0.0f;
			if (!camera->WorldPtToScreenPt3(a_world, a_x, a_y, z, 1e-5f) || z <= 0.0f) {
				return false;
			}
			if (s.guideFlipY) {
				a_y = 1.0f - a_y;
			}
			return true;
		};
		const double r = s.spec.ballRadius;
		const auto&  b = _sim->Pos();
		constexpr double kStep = 2.0;
		for (double t = r + 1.0; t <= r + 1.0 + s.guideLength; t += kStep) {
			const golf::Vec2 xy{ b.x + _aimDir.x * t, b.y + _aimDir.y * t };
			double           z = 0.0;
			const golf::Tri* tri = nullptr;
			if (!_sim->Ground(xy, b.z + 2.0 * r + t * 0.6, z, tri) || (tri && tri->surface == golf::Surface::kWater)) {
				break;  // (off the green, or into the water)
			}
			float sx = 0.0f, sy = 0.0f;
			if (project(ToWorld(frame, { xy.x, xy.y, z + 0.3 }), sx, sy)) {
				_guide.push_back({ sx, sy });
			}
		}
	}

	Golf::HudState Golf::Hud() const
	{
		std::scoped_lock lock(_hudMutex);
		auto h = _hud;
		h.power = _charging ? _power.load() : -1.0f;  // (fresher than the last tick's)
		return h;
	}
}
