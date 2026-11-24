#pragma once

#include <optional>

#include "sim/Bot.h"
#include "sim/Course.h"
#include "sim/Sim.h"

// The mini golf course in the game: the courses placed in settlements (their moving obstacles turned every frame,
// whether anyone's playing or not), and your round on one.
//
// Activating a course starts a round at hole 1. The view is the free camera, orbiting the ball: you aim by looking,
// hold the left mouse button and pull the mouse back for power, let go to putt. The right button looks down on the
// whole hole. Between holes you're moved to the next tee: no walking. X ends the round.
//
// Holding the activate key on the course (rather than tapping it) brings up a list of who's about to play, as
// Playable Pool's (settlers, companions: grown-up, friendly people near you, nearest first; up/down arrows pick, the
// activate key on the course plays them). They play each hole after you, their shots worked out by the physics (Bot) on a
// worker thread, made less than perfect by [Opponent] fSkill. Lowest total over the 18 wins.
//
// Threads: Tick runs on the game's main thread (queued from the render thread every frame), as do the camera hooks
// (the camera's update) and Animate. OnPresent and PollInput run on the render thread; Hud() on the UI's (both take
// copies under _hudMutex or atomics).
namespace mg
{
	class Golf
	{
	public:
		static bool Paused() { return IsGamePaused(); }  // (the HUD: nothing of ours drawn over a menu)
		static Golf& Get()
		{
			static Golf instance;
			return instance;
		}

		// game data ready: the course (course.txt) read, activations and loading screens listened for
		void Init();

		// render thread, every frame: the mouse and keys read, the main thread's tick queued
		void OnPresent();

		// every camera update (any camera state; main thread): the courses' obstacles turned, the ball and the putter
		// placed
		void Animate();

		// saves: our objects (the ball, the putter, the guide) gone from the world for the save, and back after
		void BeforeSave();
		void AfterSave();

		// a save loading, the main menu: the round over, everything of ours forgotten
		void Abort(const char* a_why);

		// camera hooks (CameraHook.cpp)
		bool OnLookMouse(const RE::MouseMoveEvent* a_event);
		void BeforeFreeCameraUpdate(RE::FreeCameraState* a_free);
		void OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before);
		void OnFreeCameraUpdate(RE::FreeCameraState* a_free);
		bool FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out);
		bool SwallowButton(const RE::ButtonEvent* a_event);

		bool Playing() const { return _playing.load(); }

		// main thread: a course activated by you (a round starts on it)
		void Activated(RE::TESObjectREFR* a_ref);

		// what the HUD shows: a copy (the UI's thread)
		struct HudState
		{
			bool             playing{ false };
			int              hole{ 0 };          // 1..18
			std::string      name;
			int              par{ 0 };
			int              stroke{ 0 };        // strokes taken on this hole
			int              total{ 0 };         // strokes so far, all holes
			int              parSoFar{ 0 };      // the par of the holes finished
			std::vector<int> card;               // each hole's strokes (0 not played yet)
			std::vector<int> pars;
			float            power{ -1.0f };     // charging: 0..1, else -1
			bool             overhead{ false };
			std::string      banner;             // big, in the middle ("HOLE IN ONE!", "WATER  +1")
			std::string      bannerSub;
			float            bannerAge{ 99.0f };
			bool             good{ true };       // the banner's colour
			bool             showCard{ false };  // the scorecard up (after a hole, at the end)
			bool             finished{ false };  // the round's over: the final card
			std::string      keys;               // the key hints
			std::vector<std::array<float, 2>> guide;  // the aim line: screen points 0..1, from the ball's edge outwards
			// a 1v1 against a settler (opponent empty: solo)
			std::string      opponent;           // their name, upper case
			std::vector<int> oppCard;
			int              oppTotal{ 0 };
			int              oppParSoFar{ 0 };
			bool             oppTurn{ false };   // they're playing the hole now
			// the who-to-play list (hold the activate key on the course): names, the one picked
			std::vector<std::string> pick;
			int                      pickSel{ 0 };
			std::string              pickHint;
		};
		HudState Hud() const;

	private:
		Golf() = default;

		struct Frame  // the course in the world
		{
			RE::NiMatrix3 rot;  // rows: the course's x, y, z axes in the world
			RE::NiPoint3  origin;
			float         scale{ 1.0f };
		};

		enum class Phase
		{
			kAim,      // the ball at rest: aim and putt
			kRolling,  // the shot's going
			kPenalty,  // in the water / out of bounds: the ball back after a moment
			kHoled,    // in the cup (or the stroke limit): the next hole after a moment
			kDone      // the round's over: the card, then out
		};

		// main thread
		void Tick();
		void Scan();
		void UpdateGuide();
		void PlayNearest();
		void DecidePending();  // a course activated: a tap plays solo, a hold a settler
		void StartRound(RE::TESObjectREFR* a_course, RE::Actor* a_opponent = nullptr);
		void StartHole(int a_index);
		void EndRound(const char* a_why);
		void Putt(float a_power);
		void Strike(golf::Vec2 a_dir, double a_speed, float a_loudness);
		void TickPlay(float a_dt);
		void ShotEvents(const std::vector<golf::Event>& a_events);
		void FinishHole(bool a_holed);
		void Banner(std::string a_title, std::string a_sub, bool a_good);
		void MovePlayerToTee();
		void BlockControls(bool a_block);
		void EnterCamera();
		void LeaveCamera();
		void HoldPlayerView();
		void PlaceObjects();
		void RemoveObjects();
		void UpdateHud();
		static bool IsGamePaused();

		// the settler you're playing
		RE::Actor*  Opponent() const;
		std::vector<RE::Actor*> PlayersNear() const;  // who you could play: near you, nearest first (7 at most)
		void        OpenPick(RE::TESObjectREFR* a_course);
		void        ClosePick();
		void        StartOpponentTurn();
		void        TickOpponent();
		void        PlanOpponentShot();
		void        PlaceOpponent(const golf::Vec3& a_stand, const golf::Vec3& a_look);
		void        OpponentAtBall();     // beside their ball, facing the aim
		void        OpponentWatching();   // to the side of the tee while you play
		void        PlayerWatching();     // you, off to the side while they play
		void        ReleaseOpponent();
		void        ShowOpponent(bool a_show);  // their 3D culled or not ([Opponent] bHide)
		void        DropOpponent(const char* a_why);  // gone mid-round: you play on alone
		bool        Versus() const { return static_cast<bool>(_opponent); }
		std::vector<int>& ActiveCard() { return _oppTurn ? _oppCard : _card; }

		bool         FrameOf(RE::TESObjectREFR* a_course, Frame& a_out) const;
		RE::NiPoint3 ToWorld(const Frame& a_frame, const golf::Vec3& a_p) const;
		golf::Vec2   DirToCourse(const Frame& a_frame, float a_worldYaw) const;  // a game heading -> a course direction
		float        YawOf(const Frame& a_frame, const golf::Vec2& a_dir) const;  // ...and back

		RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos);
		static void         Remove(RE::ObjectRefHandle& a_handle);
		static void         Place(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, bool a_hidden);

		// render thread
		void PollInput();

		// ---- the course (course.txt)
		golf::Course _course;
		bool         _loaded{ false };
		double       _clock{ 0.0 };  // the obstacles' clock (seconds): every course's obstacles go by it

		// the courses near you (Scan): their 3D's obstacle nodes, found by name once per 3D
		struct Placed
		{
			RE::ObjectRefHandle           ref;
			RE::NiAVObject*               root{ nullptr };
			std::vector<RE::NiAVObject*>  nodes;  // [hole][obstacle], flattened in course order
		};
		std::vector<Placed> _placed;
		float               _scanTimer{ 0.0f };
		bool                _toldHowToPlay{ false };
		bool                _cameraOffAfterLoad{ false };  // the free camera still to switch off (LeaveCamera under a loading screen)  // the first time you're near a course: how to play, once

		// ---- the activate key: tapped or held on the course
		std::atomic<int>                      _activateVk{ 0 };
		std::atomic<bool>                     _activateDown{ false };
		std::atomic<std::int64_t>             _activateDownAt{ 0 }, _activateUpAt{ 0 };  // (steady clock, ms)
		RE::ObjectRefHandle                   _pendingCourse;  // activated: waiting to see if it's a hold
		float                                 _pendingAge{ 0.0f };
		static constexpr std::int64_t         kHoldMs = 500;

		// ---- the who-to-play list
		struct Pick
		{
			RE::ObjectRefHandle              course;
			std::vector<RE::ObjectRefHandle> who;
			std::vector<std::string>         names;
			int                              sel{ 0 };
			float                            idle{ 0.0f };  // seconds since it was last touched
		};
		std::optional<Pick> _pick;
		std::atomic<bool>   _picking{ false };   // (the render thread: the arrows go to it)
		std::atomic<int>    _pickMove{ 0 };      // arrow presses not yet applied (render thread -> main)
		std::atomic<bool>   _pickCancel{ false };  // the leave key (or Backspace) pressed with the list up

		// ---- the controller (render thread; Pad.h)
		std::uint16_t                         _padWas{ 0 };       // its buttons last frame
		bool                                  _padRtWas{ false };
		bool                                  _padCharging{ false };  // this putt's power is from the stick
		float                                 _padBHeld{ 0.0f };  // seconds B's been held (a second ends the round)
		std::chrono::steady_clock::time_point _padLastPoll{};
		std::atomic<bool>                     _padInUse{ false };  // the controller was used last (the HUD's hints)
		std::array<bool, 2> _arrowWas{};

		// ---- the settler you're playing (none: solo)
		RE::ObjectRefHandle _opponent;
		std::string         _opponentName;
		std::vector<int>    _oppCard;
		std::atomic<bool>   _oppTurn{ false };
		struct Plan  // their next shot, worked out on a worker thread
		{
			std::atomic<bool> done{ false };
			golf::Shot        shot;
		};
		std::shared_ptr<Plan> _plan;
		double                _planFrom{ 0.0 };   // the clock when it was asked for
		double                _planLead{ 0.0 };   // ...it plans the ball as it'll be this long after
		double                _strikeAt{ -1.0 };  // the clock their putt goes at (-1: no shot planned yet)
		golf::Shot            _oppShot;
		int                   _planTries{ 0 };
		std::uint32_t         _planSeed{ 1 };

		// ---- your round
		std::atomic<bool>          _playing{ false };
		RE::ObjectRefHandle        _courseRef;
		int                        _hole{ 0 };  // index into _course.holes
		std::unique_ptr<golf::Sim> _sim;
		std::atomic<Phase>         _phase{ Phase::kAim };  // (read on the render thread too)
		float                      _phaseTime{ 0.0f };
		std::vector<int>           _card;     // strokes per hole
		int                        _stroke{ 0 };
		golf::Vec3                 _lastRest;  // where the last shot was played from (a penalty puts it back)
		golf::Vec2                 _aimDir{ 1.0, 0.0 };
		float                      _shotSoundGap{ 0.0f };

		// the objects we place: the ball, the guide line's pieces
		RE::ObjectRefHandle              _ball;
		std::vector<RE::ObjectRefHandle> _dots;
		bool                             _saving{ false };

		// controls
		RE::BSTSmartPointer<RE::BSInputEnableLayer> _inputLayer;  // (a layer of our own)
		bool                                        _controlsBlocked{ false };
		std::atomic<bool>                           _lmbWas{ false }, _rmbWas{ false };
		std::array<bool, 256>                       _keysWas{};

		// the camera (the camera hooks lock _camMutex)
		mutable std::mutex    _camMutex;
		std::atomic<bool>     _inCamera{ false };
		bool                  _camMapped{ false };
		int                   _mapFrames{ 0 };
		int                   _yawIndex{ 0 };      // which of the free camera's two angles is the yaw
		float                 _pitchSign{ 1.0f };
		float                 _entryYaw{ 0.0f }, _entryPitch{ 0.0f };
		RE::NiPoint2          _entryFreeRotation{};
		bool                  _haveEntryView{ false };
		RE::NiPoint3          _camTarget{}, _camDesired{};
		bool                  _haveCamTarget{ false };
		float                 _camYaw{ 0.0f };     // the view's heading now (the aim)
		float                 _wantYaw{ 0.0f };    // a new hole: the view turned to this...
		bool                  _turnTo{ false };    // ...once
		std::atomic<bool>     _overhead{ false };
		RE::NiPoint3          _overheadAt{};
		float                 _overheadHeight{ 300.0f };
		std::chrono::steady_clock::time_point _lastCam{};

		// charging a putt
		std::atomic<bool>  _charging{ false };
		std::atomic<float> _power{ 0.0f };
		const RE::MouseMoveEvent* _lastMouseEvent{ nullptr };
		std::uint32_t      _lastMouseTime{ 0 };

		// timing
		std::atomic<bool>                     _tickPending{ false };
		std::chrono::steady_clock::time_point _last{};
		bool                                  _haveLast{ false };

		// the HUD
		std::vector<std::array<float, 2>> _guide;
		mutable std::mutex _hudMutex;
		HudState           _hud;
		std::string        _banner, _bannerSub;
		float              _bannerAge{ 99.0f };
		bool               _bannerGood{ true };
		bool               _showCard{ false };
	};
}
