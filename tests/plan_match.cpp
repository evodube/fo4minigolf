// A settler's putt planned on a copy (1/100 s steps) must come out the same played in the game's uneven frames.
//     g++ -std=c++20 -O2 -Isrc tests/plan_match.cpp src/sim/Sim.cpp src/sim/Course.cpp src/sim/Bot.cpp -pthread -o plan_match
//     ./plan_match dist/Data/F4SE/Plugins/fo4minigolf/course.txt

#include "sim/Bot.h"
#include "sim/Course.h"
#include "sim/Sim.h"
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
using namespace golf;
int main(int, char** argv)
{
	std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
	Course c; std::string err; Course::Parse(ss.str(), c, err);
	std::mt19937 rng(3); std::uniform_real_distribution<double> fr(1.0 / 144, 1.0 / 30);
	int bad = 0;
	for (const auto& h : c.holes) {
		Sim base(h); base.SetTime(12.345);
		Bot bot(h);
		Sim plan = base; plan.Step(2.0);
		Shot s = bot.Choose(plan, 36, 8, 1, 2);
		// as the game: frames until the strike time, strike, frames until rest
		Sim game = base; const double strikeAt = 12.345 + 2.0 + s.wait;
		while (game.Time() + 1.0 / 30 < strikeAt) game.Step(fr(rng));
		game.Step(strikeAt - game.Time());  // (the game steps to the very moment, then putts)
		game.Strike(s.dir, s.speed);
		for (int i = 0; i < 20000 && game.Moving(); ++i) game.Step(fr(rng));
		const double d = (game.Pos().XY() - s.rest.XY()).Length();
		bad += d > 3.0 || game.State() != s.end;
		std::printf("hole %2d plan state %d at %.1f,%.1f  game state %d at %.1f,%.1f  (%.2f off)\n", h.number, int(s.end), s.rest.x, s.rest.y,
			int(game.State()), game.Pos().x, game.Pos().y, d);
	}
	std::printf("%d differ\n", bad);
	return bad ? 1 : 0;
}
