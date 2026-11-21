#pragma once

#include "sim/Course.h"
#include "sim/Sim.h"

// The mini golf course in the game: the courses placed in settlements (their moving obstacles turned every frame,
// whether anyone's playing or not), and your round on one.
//
// Activating a course starts a round at hole 1. The view is the free camera, orbiting the ball: you aim by looking,
// hold the left mouse button and pull the mouse back for power, let go to putt. The right button looks down on the
// whole hole. Between holes you're moved to the next tee: no walking. X ends the round.
//
// Threads: Tick runs on the game's main thread (queued from the render thread every frame), as do the camera hooks
// (the camera's update) and Animate. OnPresent and PollInput run on the render thread; Hud() on the UI's (both take
// copies under _hudMutex or atomics).
namespace mg
{
	class Golf
	{
	public:
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
		void PlayNearest();
		void StartRound(RE::TESObjectREFR* a_course);
		void StartHole(int a_index);
		void EndRound(const char* a_why);
		void Putt(float a_power);
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

		// the objects we place: the ball, the putter, the guide's dots
		RE::ObjectRefHandle              _ball;
		RE::ObjectRefHandle              _putter;
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
		mutable std::mutex _hudMutex;
		HudState           _hud;
		std::string        _banner, _bannerSub;
		float              _bannerAge{ 99.0f };
		bool               _bannerGood{ true };
		bool               _showCard{ false };
	};
}
