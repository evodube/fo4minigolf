// Every ball stops: thousands of putts from all over every hole (on the slopes, by the rails, at the obstacles), at
// every speed, must each come to rest, drop in or go out within the shot time - never "moving" for ever, which left
// the game waiting for a turn that never came.
//     g++ -std=c++20 -O2 -Isrc tests/stuck_test.cpp src/sim/Sim.cpp src/sim/Course.cpp -o stuck_test
//     ./stuck_test dist/Data/F4SE/Plugins/fo4minigolf/course.txt

#include "sim/Course.h"
#include "sim/Sim.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
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
	std::mt19937                           rng(11);
	std::uniform_real_distribution<double> u(0.0, 1.0);
	std::uniform_real_distribution<double> frame(1.0 / 144, 1.0 / 30);
	int    bad = 0, total = 0;
	double longest = 0.0;
	for (const auto& h : c.holes) {
		for (int k = 0; k < 300; ++k) {
			Sim sim(h);
			const Vec2 at{ h.minx + u(rng) * (h.maxx - h.minx), h.miny + u(rng) * (h.maxy - h.miny) };
			if (!sim.Place(at)) {
				continue;
			}
			sim.SetTime(u(rng) * 20.0);
			const double a = u(rng) * 6.283185307179586;
			sim.Strike({ std::cos(a), std::sin(a) }, 5.0 + u(rng) * u(rng) * 400.0);
			++total;
			double t = 0.0;
			while (sim.Moving() && t < 25.0) {
				const double dt = frame(rng);
				sim.Step(dt);
				t += dt;
			}
			longest = std::max(longest, t);
			if (sim.Moving()) {
				++bad;
				std::printf("hole %d: still moving after 25 s, at (%.1f %.1f %.1f)\n", h.number, sim.Pos().x, sim.Pos().y, sim.Pos().z);
			}
		}
	}
	std::printf("%d putts, %d never stopped, the longest %.1f s\n", total, bad, longest);
	return bad ? 1 : 0;
}
