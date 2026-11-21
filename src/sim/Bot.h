#pragma once

// A player made of the physics: picks a shot by trying lots of them on a copy of the simulation and keeping the
// one that ends nearest the cup, measured along the ground round the walls (a distance field from the cup), not
// as the crow flies. Used by the tests to prove every hole can be played, and later for settlers who play.

#include "sim/Sim.h"

#include <cstdint>
#include <vector>

namespace golf
{
	struct Shot
	{
		Vec2      dir;
		double    speed{ 0.0 };
		double    wait{ 0.0 };  // seconds to wait before hitting it (timing an obstacle)
		double    score{ 1e18 };
		BallState end{ BallState::kRest };
		Vec3      rest;
	};

	class Bot
	{
	public:
		Bot(const Hole& a_hole, const Spec& a_spec = {});

		// How far a ball at a_pos has to roll to the cup, round the walls. Large if it can't get there.
		double Distance(Vec3 a_pos) const;

		// The best shot from where a_sim's ball is now, out of a_dirs directions x a_speeds speeds (x a few waits,
		// if the hole has moving obstacles). a_seed varies the directions tried a little.
		Shot Choose(const Sim& a_sim, int a_dirs = 72, int a_speeds = 12, std::uint32_t a_seed = 1) const;

		// Plays a shot on a copy and says how it ends.
		Shot Try(const Sim& a_sim, Vec2 a_dir, double a_speed, double a_wait) const;

	private:
		static constexpr double kCell = 4.0;

		const Hole*         _hole;
		Spec                _spec;
		double              _x0{ 0 }, _y0{ 0 };
		int                 _nx{ 0 }, _ny{ 0 };
		std::vector<double> _dist;   // per cell, along the ground to the cup; < 0 = no ground
		std::vector<double> _floor;  // per cell, the ground's height
	};
}
