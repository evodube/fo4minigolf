// Plays the real course as a settler would (Bot::Settler: the cup's way, a skill's misses), and checks no putt loses
// ground or lands in a hazard when one that doesn't exists.
//     g++ -std=c++20 -O2 -Isrc tests/settler_play.cpp src/sim/Sim.cpp src/sim/Course.cpp src/sim/Bot.cpp -pthread -o settler_play
//     ./settler_play dist/Data/F4SE/Plugins/fo4minigolf/course.txt [skill]

#include "sim/Bot.h"
#include "sim/Course.h"
#include "sim/Sim.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace golf;

int main(int argc, char** argv)
{
	std::ifstream f(argc > 1 ? argv[1] : "dist/Data/F4SE/Plugins/fo4minigolf/course.txt");
	std::stringstream ss;
	ss << f.rdbuf();
	Course      c;
	std::string err;
	if (!Course::Parse(ss.str(), c, err)) {
		std::printf("parse: %s\n", err.c_str());
		return 1;
	}
	const double skill = argc > 2 ? std::atof(argv[2]) : 0.6;
	int          total = 0, par = 0, backwards = 0, hazards = 0;
	double       worst = 0.0;
	for (const auto& h : c.holes) {
		Sim  sim(h);
		Bot  bot(h);
		int  strokes = 0;
		Vec3 from = sim.Pos();
		while (strokes < 6) {
			const double before = bot.Distance(sim.Pos());
			const auto   t0 = std::chrono::steady_clock::now();
			const Shot   s = bot.Settler(sim, skill, 1000u * h.number + strokes, 4);
			worst = std::max(worst, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
			sim.Step(s.wait);
			sim.Strike(s.dir, s.speed);
			for (int i = 0; i < 3000 && sim.Moving(); ++i) {
				sim.Step(0.01);
			}
			++strokes;
			if (sim.State() == BallState::kInCup) {
				break;
			}
			if (sim.State() == BallState::kWater || sim.State() == BallState::kOut) {
				++hazards;
				++strokes;
				sim.Place(from.XY(), from.z + 1.0);
			} else if (bot.Distance(sim.Pos()) > before + 1.0) {
				++backwards;
				std::printf("  hole %d putt %d lost ground: %.0f -> %.0f\n", h.number, strokes, before, bot.Distance(sim.Pos()));
			}
			from = sim.Pos();
		}
		total += strokes;
		par += h.par;
		std::printf("hole %2d par %d: %d\n", h.number, h.par, strokes);
	}
	std::printf("total %d (par %d), %d lost ground, %d hazards, slowest putt %.1f s\n", total, par, backwards, hazards, worst);
	return 0;
}
