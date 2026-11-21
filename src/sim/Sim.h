#pragma once

// Mini golf ball physics: one ball on one hole. Plain C++ with no game dependencies (tests/sim_test.cpp).
//
// 2.5D: the ground is the hole's triangles (any number of levels: a tunnel under a green works), walls are vertical
// segments with a height range, and moving obstacles are walls that turn, swing or slide on a clock. The ball rolls
// on the ground with gravity along the slope (a rolling solid sphere: 5/7 g sin a), flies when it runs off an edge
// or over a crest fast enough, and bounces when it lands. The cup is a hole in the ground with a round rim the ball
// can catch and lip out on: a slow ball drops in, a fast one skips over.
//
// Units: game units (about 70 per metre) and seconds, in course space (see Course.h).

#include "sim/Course.h"

#include <vector>

namespace golf
{
	struct Spec
	{
		double ballRadius{ 1.5 };         // 42.7 mm
		double gravity{ 687.0 };          // 9.81 m/s^2
		double rollDecel{ 38.0 };         // rolling resistance on carpet, units/s^2 (a 2 m/s putt rolls about 3.5 m)
		double fastFactor{ 0.35 };        // rolling resistance on "fast" ground, times rollDecel
		double slowFactor{ 4.0 };         // and on "slow" ground
		double landRestitution{ 0.35 };   // bounce when landing
		double rimRestitution{ 0.45 };
		double stopSpeed{ 1.5 };          // slower than this (and the slope can't move it): at rest
		double stepUp{ 0.6 };             // in ball radii: a seam this high is rolled over
		double launchSpeed{ 25.0 };       // moving away from the ground faster than this at a crest: airborne
		double maxSpeed{ 420.0 };         // full power, 6 m/s
		double maxShotTime{ 30.0 };       // a shot still going after this is stopped
	};

	enum class BallState : std::uint8_t
	{
		kRest,
		kRolling,
		kAir,
		kInCup,   // holed
		kWater,   // in a water hazard
		kOut      // out of bounds
	};

	struct Event
	{
		enum class Type : std::uint8_t
		{
			kWall,      // hit a wall (b = the obstacle's index, or -1 for a still wall)
			kLand,      // landed after flying
			kRim,       // touched the cup's rim
			kCup,       // holed
			kWater,
			kOut,
			kRest       // came to rest
		};

		Type   type;
		int    b{ -1 };
		double speed{ 0.0 };  // impact speed, for sounds
		Vec3   pos;
	};

	struct Quat
	{
		double w{ 1.0 }, x{ 0.0 }, y{ 0.0 }, z{ 0.0 };
		Quat   operator*(const Quat& o) const;
		void   Normalize();
		void   ToMatrix(double out[3][3]) const;  // row-major
	};

	class Sim
	{
	public:
		Sim(const Hole& a_hole, const Spec& a_spec = {});

		const Hole& GetHole() const { return *_hole; }
		const Spec& GetSpec() const { return _spec; }

		// Puts the ball at rest on the highest ground under (x, y) that's no higher than a_zHint (so a ball put back
		// in a tunnel stays in it). false if there's no ground there.
		bool Place(Vec2 a_xy, double a_zHint = 1e9);
		// Puts the ball on the tee.
		bool PlaceOnTee() { return Place(_hole->tee.XY(), _hole->tee.z + 1.0); }

		// Hits the ball along a_dir (any length) at a_speed (clamped to maxSpeed).
		void Strike(Vec2 a_dir, double a_speed);

		// Advances the ball and the obstacles' clock, in small sub-steps.
		std::vector<Event> Step(double a_dt);

		BallState   State() const { return _state; }
		bool        Moving() const { return _state == BallState::kRolling || _state == BallState::kAir; }
		const Vec3& Pos() const { return _pos; }
		const Vec3& Vel() const { return _vel; }
		const Quat& Rot() const { return _rot; }
		double      Time() const { return _time; }
		void        SetTime(double a_time) { _time = a_time; }

		// An obstacle's pose now (for the game to move its mesh): see Obstacle::Pose.
		void ObstaclePose(int a_index, double& a_angle, Vec2& a_offset, double& a_lift) const;

		// The surface height under (x, y) at or below a_zTop, and its triangle; false if none.
		bool Ground(Vec2 a_xy, double a_zTop, double& a_z, const Tri*& a_tri) const;

		// Where a shot would go, for the aiming guide: points every a_spacing units along the ball's path, for at
		// most a_maxLength units (the obstacles keep moving, as they will).
		std::vector<Vec3> Predict(Vec2 a_dir, double a_speed, double a_maxLength, double a_spacing) const;

	private:
		void SubStep(double a_h, std::vector<Event>& a_events);
		void Roll(double a_h, std::vector<Event>& a_events);
		void Fly(double a_h, std::vector<Event>& a_events);
		void CollideWalls(std::vector<Event>& a_events);
		bool CollideWall(const Wall& a_wall, Vec2 a_a, Vec2 a_b, int a_obstacle, std::vector<Event>& a_events);
		void CollideRim(std::vector<Event>& a_events);
		void CheckFinish(std::vector<Event>& a_events);
		bool OverCup(Vec2 a_xy) const;
		double CentreZ(double a_surface, const Tri* a_tri) const;
		double Resistance(const Tri* a_tri) const;
		void Spin(double a_h);

		const Hole* _hole;
		Spec        _spec;
		BallState   _state{ BallState::kRest };
		Vec3        _pos;
		Vec3        _vel;
		Quat        _rot;
		const Tri*  _ground{ nullptr };
		double      _time{ 0.0 };
		double      _shotTime{ 0.0 };
	};
}
