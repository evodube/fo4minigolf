// Plays the real course with the bot: every hole must be holed within par + 3.
//     g++ -std=c++20 -O2 -Isrc tests/course_play.cpp src/sim/Sim.cpp src/sim/Course.cpp src/sim/Bot.cpp -o course_play
//     ./course_play dist/Data/F4SE/Plugins/fo4minigolf/course.txt

#include "sim/Bot.h"
#include "sim/Course.h"
#include "sim/Sim.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace golf;

int main(int argc, char** argv)
{
	const char*   path = argc > 1 ? argv[1] : "dist/Data/F4SE/Plugins/fo4minigolf/course.txt";
	std::ifstream f(path);
	if (!f) {
		std::printf("no %s\n", path);
		return 1;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	Course      c;
	std::string err;
	if (!Course::Parse(ss.str(), c, err)) {
		std::printf("parse: %s\n", err.c_str());
		return 1;
	}
	int failed = 0, total = 0, par = 0;
	for (const auto& h : c.holes) {
		if (argc > 2 && h.number != std::atoi(argv[2])) {
			continue;
		}
		Sim sim(h);
		Bot bot(h);
		Vec3 from = sim.Pos();
		int  strokes = 0;
		std::string log;
		while (strokes < 10) {
			const Shot s = bot.Choose(sim, 60, 12, strokes + 1);
			if (s.wait > 0) {
				sim.Step(s.wait);
			}
			sim.Strike(s.dir, s.speed);
			for (int i = 0; i < 3000 && sim.Moving(); ++i) {
				sim.Step(1.0 / 100.0);
			}
			++strokes;
			char buf[160];
			std::snprintf(buf, sizeof buf, " [%.0f %.0f -> st%d %.0f,%.0f d%.0f]", s.speed, s.wait, int(sim.State()), sim.Pos().x,
				sim.Pos().y, bot.Distance(sim.Pos()));
			log += buf;
			if (sim.State() == BallState::kInCup) {
				break;
			}
			if (sim.State() == BallState::kWater || sim.State() == BallState::kOut) {
				++strokes;  // penalty, back to where it was played from
				sim.Place(from.XY(), from.z + 1.0);
			}
			from = sim.Pos();
		}
		const bool ok = sim.State() == BallState::kInCup && strokes <= h.par + 3;
		failed += !ok;
		total += strokes;
		par += h.par;
		std::printf("%s hole %2d %-16s par %d: %2d strokes%s\n", ok ? "ok  " : "FAIL", h.number, h.name.c_str(), h.par, strokes,
			ok ? "" : log.c_str());
	}
	std::printf("total %d (par %d), %d failed\n", total, par, failed);
	return failed ? 1 : 0;
}
