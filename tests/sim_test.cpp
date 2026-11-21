// Tests for the mini golf physics and the course file. Plain C++:
//     g++ -std=c++20 -O2 -Isrc tests/sim_test.cpp src/sim/Sim.cpp src/sim/Course.cpp -o sim_test && ./sim_test

#include "sim/Course.h"
#include "sim/Sim.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace golf;

static int g_failed = 0;

#define CHECK(cond, ...)                                    \
	do {                                                    \
		if (!(cond)) {                                      \
			std::printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
			std::printf(__VA_ARGS__);                       \
			std::printf("\n");                              \
			++g_failed;                                     \
		}                                                   \
	} while (0)

namespace
{
	// a rectangle of ground from (x0, y0) to (x1, y1), z0 at y0 and z1 at y1 (a slope along y)
	void Quad(Hole& h, double x0, double y0, double x1, double y1, double z0, double z1, Surface s = Surface::kGreen)
	{
		Tri a{ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z1 }, s, {} };
		Tri b{ { x0, y0, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, s, {} };
		h.tris.push_back(a);
		h.tris.push_back(b);
	}

	void Box(Hole& h, double x0, double y0, double x1, double y1, double z0, double z1, double e = 0.6)
	{
		h.walls.push_back({ { x0, y0 }, { x1, y0 }, z0, z1, e });
		h.walls.push_back({ { x1, y0 }, { x1, y1 }, z0, z1, e });
		h.walls.push_back({ { x1, y1 }, { x0, y1 }, z0, z1, e });
		h.walls.push_back({ { x0, y1 }, { x0, y0 }, z0, z1, e });
	}

	// a flat lane along +y, 60 wide, 800 long, walled, with the cup far away unless moved
	Hole Lane()
	{
		Hole h;
		h.number = 1;
		h.par = 2;
		h.tee = { 0, 0, 0 };
		h.cup = { 0, 5000, 0 };
		Quad(h, -30, -20, 30, 800, 0, 0);
		Box(h, -30, -20, 30, 800, -5, 6);
		h.minx = -100;
		h.maxx = 100;
		h.miny = -100;
		h.maxy = 900;
		h.minz = -50;
		h.Finish();
		return h;
	}

	BallState Run(Sim& s, double a_seconds = 20.0)
	{
		for (int i = 0; i < int(a_seconds * 60) && s.Moving(); ++i) {
			s.Step(1.0 / 60.0);
		}
		return s.State();
	}
}

static void TestRollDistance()
{
	Hole h = Lane();
	Sim  s(h);
	CHECK(s.State() == BallState::kRest, "placed");
	CHECK(std::abs(s.Pos().z - 1.5) < 1e-9, "z %f", s.Pos().z);
	s.Strike({ 0, 1 }, 140.0);
	Run(s);
	const double expect = 140.0 * 140.0 / (2.0 * s.GetSpec().rollDecel);
	CHECK(s.State() == BallState::kRest, "state %d", int(s.State()));
	CHECK(std::abs(s.Pos().y - expect) < 6.0, "rolled %.1f, expected about %.1f", s.Pos().y, expect);
	CHECK(std::abs(s.Pos().x) < 1e-6, "drifted %f", s.Pos().x);
	CHECK(std::abs(s.Pos().z - 1.5) < 1e-6, "z %f", s.Pos().z);
}

static void TestWallBounce()
{
	Hole h = Lane();
	Sim  s(h);
	s.Place({ 0, 100 });
	s.Strike({ 1, 0 }, 100.0);
	bool hit = false;
	for (int i = 0; i < 60 && !hit; ++i) {
		for (const auto& e : s.Step(1.0 / 120.0)) {
			if (e.type == Event::Type::kWall) {
				hit = true;
			}
		}
	}
	CHECK(hit, "hit the wall");
	CHECK(s.Vel().x < 0.0, "bounced back: vx %f", s.Vel().x);
	CHECK(std::abs(s.Vel().x) > 45.0 && std::abs(s.Vel().x) < 65.0, "restitution: vx %f", s.Vel().x);
	CHECK(s.Pos().x <= 30 - 1.5 + 1e-6, "inside: x %f", s.Pos().x);
}

static void TestCup()
{
	// slow: in
	{
		Hole h = Lane();
		h.cup = { 0, 200, 0 };
		Sim s(h);
		// arrive at about 70 units/s: v0^2 = 70^2 + 2 * decel * 200
		const double v0 = std::sqrt(70.0 * 70.0 + 2.0 * s.GetSpec().rollDecel * (200.0 - 3.78));
		s.Strike({ 0, 1 }, v0);
		CHECK(Run(s) == BallState::kInCup, "slow putt state %d at y %.1f", int(s.State()), s.Pos().y);
	}
	// too fast: skips over the cup and rolls on
	{
		Hole h = Lane();
		h.cup = { 0, 200, 0 };
		Sim s(h);
		s.Strike({ 0, 1 }, 330.0);
		CHECK(Run(s) == BallState::kRest, "fast putt state %d", int(s.State()));
		CHECK(s.Pos().y > 300.0, "rolled on to %.1f", s.Pos().y);
	}
	// slow but off-centre by nearly a cup radius: catches the edge (in or lips out, but it's slowed and turned)
	{
		Hole h = Lane();
		h.cup = { 0, 200, 0 };
		Sim s(h);
		s.Place({ 3.2, 0 });
		const double v0 = std::sqrt(60.0 * 60.0 + 2.0 * s.GetSpec().rollDecel * 200.0);
		s.Strike({ 0, 1 }, v0);
		bool rim = false;
		for (int i = 0; i < 1200 && s.Moving(); ++i) {
			for (const auto& e : s.Step(1.0 / 60.0)) {
				rim |= e.type == Event::Type::kRim;
			}
		}
		CHECK(rim || s.State() == BallState::kInCup, "edge putt touched the rim (state %d)", int(s.State()));
	}
	// a ball rolling to a stop right at the lip falls in
	{
		Hole h = Lane();
		h.cup = { 0, 200, 0 };
		Sim s(h);
		const double v0 = std::sqrt(2.0 * s.GetSpec().rollDecel * (200.0 - 3.78 + 0.3));
		s.Strike({ 0, 1 }, v0);
		CHECK(Run(s) == BallState::kInCup, "dying putt state %d at y %.2f", int(s.State()), s.Pos().y);
	}
}

static void TestSlopes()
{
	// steep: 20% grade, the ball rolls down by itself once nudged
	{
		Hole h = Lane();
		h.tris.clear();
		Quad(h, -30, -20, 30, 800, 160, 0);  // falls along +y
		h.Finish();
		Sim s(h);
		s.Place({ 0, 0 });
		s.Strike({ 0, 1 }, 2.0);
		Run(s, 4.0);
		CHECK(s.Pos().y > 200.0, "rolled down to %.1f", s.Pos().y);
		CHECK(s.Vel().y > 50.0, "speeding up: vy %.1f", s.Vel().y);
	}
	// gentle: 2% grade, it stays put
	{
		Hole h = Lane();
		h.tris.clear();
		Quad(h, -30, -20, 30, 800, 16, 0);
		h.Finish();
		Sim s(h);
		s.Place({ 0, 0 });
		s.Strike({ 0, 1 }, 2.0);
		Run(s, 4.0);
		CHECK(s.State() == BallState::kRest, "rests on a gentle slope (state %d)", int(s.State()));
		CHECK(s.Pos().y < 5.0, "barely moved: %.2f", s.Pos().y);
	}
	// uphill putt comes back down past where it started
	{
		Hole h = Lane();
		h.tris.clear();
		Quad(h, -30, -20, 30, 800, -16, 144);  // rises along +y (20%)
		h.Finish();
		Sim s(h);
		s.Place({ 0, 100 });
		s.Strike({ 0, 1 }, 150.0);
		Run(s);
		CHECK(s.Pos().y < 100.0, "rolled back down to %.1f", s.Pos().y);
	}
}

static void TestRampJump()
{
	// flat run-up, a 30 degree ramp, a gap with nothing under it (out of bounds), and a landing green
	Hole h;
	h.cup = { 0, 5000, 0 };
	Quad(h, -30, -20, 30, 100, 0, 0);
	Quad(h, -30, 100, 30, 120, 0, 11.5);  // up 11.5 over 20
	Quad(h, -30, 200, 30, 600, 0, 0);     // landing, 80 past the ramp's lip
	Box(h, -30, 200, 30, 600, -5, 6);
	h.minx = -100;
	h.maxx = 100;
	h.miny = -100;
	h.maxy = 700;
	h.minz = -30;
	h.Finish();
	{
		Sim s(h);
		s.Strike({ 0, 1 }, 330.0);
		bool flew = false, landed = false;
		for (int i = 0; i < 1200 && s.Moving(); ++i) {
			for (const auto& e : s.Step(1.0 / 120.0)) {
				landed |= e.type == Event::Type::kLand;
			}
			flew |= s.State() == BallState::kAir;
		}
		CHECK(flew, "left the ramp");
		CHECK(landed, "landed");
		CHECK(s.State() == BallState::kRest && s.Pos().y > 200.0, "made the jump: state %d y %.1f",
			int(s.State()), s.Pos().y);
	}
	{
		Sim s(h);
		s.Strike({ 0, 1 }, 150.0);
		CHECK(Run(s) == BallState::kOut, "too slow: falls in the gap (state %d, y %.1f)", int(s.State()), s.Pos().y);
	}
}

static void TestWater()
{
	Hole h = Lane();
	h.tris.clear();
	Quad(h, -30, -20, 30, 100, 0, 0);
	Quad(h, -30, 100, 30, 150, 0, 0, Surface::kWater);
	Quad(h, -30, 150, 30, 800, 0, 0);
	h.Finish();
	Sim s(h);
	s.Strike({ 0, 1 }, 100.0);
	CHECK(Run(s) == BallState::kWater, "in the water (state %d at %.1f)", int(s.State()), s.Pos().y);
}

static void TestSpinner()
{
	// a resting ball in the sweep of a spinning bar gets knocked away
	Hole h = Lane();
	Obstacle o;
	o.name = "Blade";
	o.motion = Obstacle::Motion::kSpin;
	o.pivot = { 0, 300 };
	o.period = 2.0;
	o.walls.push_back({ { 0, 300 }, { 25, 300 }, 0, 10, 0.5 });
	h.obstacles.push_back(o);
	Sim s(h);
	s.Place({ 0, 315 });  // the bar sweeps through here a quarter turn in
	bool hit = false;
	for (int i = 0; i < 240; ++i) {
		for (const auto& e : s.Step(1.0 / 60.0)) {
			hit |= e.type == Event::Type::kWall && e.b == 0;
		}
	}
	CHECK(hit, "the blade hit the ball");
	CHECK(std::abs(s.Pos().x) > 3.0 || std::abs(s.Pos().y - 315) > 3.0, "the ball moved: %.1f %.1f", s.Pos().x, s.Pos().y);
	double a, lift;
	Vec2   off;
	s.SetTime(0.5);
	s.ObstaclePose(0, a, off, lift);
	CHECK(std::abs(a - 3.14159265 / 2) < 1e-6, "quarter turn at a quarter period: %f", a);
}

static void TestMovingKinds()
{
	// wheel: 4 blades, +-20 degrees: walls there a quarter-turn's 40/90 of the time
	Obstacle w;
	w.motion = Obstacle::Motion::kWheel;
	w.period = 4.0;
	w.blades = 4;
	w.halfDegrees = 20.0;
	CHECK(w.Solid(0.0, 1.5), "blade down at 0");
	CHECK(!w.Solid(0.5, 1.5), "gap at 45 degrees");
	CHECK(w.Solid(1.0, 1.5), "next blade down at 90 degrees");
	int solid = 0;
	for (int i = 0; i < 4000; ++i) {
		solid += w.Solid(i * 0.001, 1.5);
	}
	CHECK(std::abs(solid / 4000.0 - 40.0 / 90.0) < 0.01, "duty %f", solid / 4000.0);

	// lift: shut, then open
	Obstacle l;
	l.motion = Obstacle::Motion::kLift;
	l.height = 30.0;
	l.period = 10.0;
	CHECK(l.Solid(1.0, 1.5) && !l.Solid(7.0, 1.5), "portcullis shut then open");
	double a, lift;
	Vec2   off;
	l.Pose(7.0, a, off, lift);
	CHECK(std::abs(lift - 30.0) < 1e-9, "fully up: %f", lift);

	// pendulum: axis along +x, the ball swings along y, and its walls' velocity matches their movement
	Obstacle p;
	p.motion = Obstacle::Motion::kPendulum;
	p.axisDegrees = 0.0;
	p.length = 60.0;
	p.swingDegrees = 40.0;
	p.period = 3.0;
	p.phase = 0.1;
	const Vec2 at0 = p.Move({ 5, 0 }, 1.0);
	const Vec2 at1 = p.Move({ 5, 0 }, 1.0 + 1e-5);
	const Vec2 v = p.Velocity(at0, 1.0);
	CHECK(std::abs((at1.y - at0.y) / 1e-5 - v.y) < 1e-2 && std::abs(at1.x - 5.0) < 1e-9, "pendulum velocity %f vs %f",
		(at1.y - at0.y) / 1e-5, v.y);
	p.Pose(0.75 - 0.3, a, off, lift);  // a quarter period past phase 0: the furthest swing, +40 degrees
	CHECK(std::abs(a - 40.0 * 3.14159265358979 / 180.0) < 1e-6, "pendulum angle %f", a);
	CHECK(p.Move({ 0, 0 }, 0.45).y > 38.0, "swung to +y: %f", p.Move({ 0, 0 }, 0.45).y);
}

static void TestLevels()
{
	// a green at z 40 over a tunnel floor at z 0
	Hole h = Lane();
	Quad(h, -30, 200, 30, 300, 40, 40);
	h.Finish();
	Sim s(h);
	CHECK(s.Place({ 0, 250 }), "place on top");
	CHECK(std::abs(s.Pos().z - 41.5) < 1e-6, "top z %f", s.Pos().z);
	CHECK(s.Place({ 0, 250 }, 10.0), "place in the tunnel");
	CHECK(std::abs(s.Pos().z - 1.5) < 1e-6, "tunnel z %f", s.Pos().z);
	// a putt from before the tunnel rolls through it at floor level
	s.Place({ 0, 150 });
	s.Strike({ 0, 1 }, 200.0);
	Run(s);
	CHECK(s.Pos().z < 5.0 && s.Pos().y > 300.0, "through the tunnel: y %.1f z %.1f", s.Pos().y, s.Pos().z);
}

static void TestPredict()
{
	Hole h = Lane();
	Sim  s(h);
	auto pts = s.Predict({ 0, 1 }, 140.0, 100.0, 10.0);
	CHECK(pts.size() >= 9 && pts.size() <= 13, "dots %zu", pts.size());
	CHECK(s.State() == BallState::kRest && s.Pos().y == 0.0, "the real ball didn't move");
}

static void TestParse()
{
	const std::string text =
		"course 1\n"
		"hole 1 2 Straight Shot   # a comment\n"
		"tee 0 0 0 0\n"
		"cup 0 200 0 3.78\n"
		"bounds -100 -100 100 900 -50\n"
		"tri green -30 -20 0 30 -20 0 30 800 0\n"
		"tri green -30 -20 0 30 800 0 -30 800 0\n"
		"tri water 0 0 0 1 0 0 0 1 0\n"
		"wall -30 -20 30 -20 -5 6 0.6\n"
		"obstacle Mill spin 0 100 -4 0.25\n"
		"owall 0 100 20 100 0 10 0.5\n"
		"hole 2 3 Second\n"
		"tri slow 0 0 5 10 0 5 0 10 5\n";
	Course      c;
	std::string err;
	CHECK(Course::Parse(text, c, err), "parse: %s", err.c_str());
	CHECK(c.holes.size() == 2, "holes %zu", c.holes.size());
	if (c.holes.size() == 2) {
		CHECK(c.holes[0].name == "Straight Shot", "name '%s'", c.holes[0].name.c_str());
		CHECK(c.holes[0].tris.size() == 3, "tris");
		CHECK(c.holes[0].obstacles.size() == 1 && c.holes[0].obstacles[0].walls.size() == 1, "obstacle");
		CHECK(c.holes[0].obstacles[0].period == -4.0, "period");
		CHECK(c.holes[1].par == 3 && c.holes[1].tris[0].surface == Surface::kSlow, "hole 2");
	}
	Course bad;
	CHECK(!Course::Parse("hole 1 2 x\ntri lava 0 0 0 1 0 0 0 1 0\n", bad, err), "bad surface rejected");
	CHECK(err.find("line 2") != std::string::npos, "error names the line: %s", err.c_str());
}

// Every hole of the real course: the tee has ground, the cup is on ground, and a putt from the tee can be played.
static void TestRealCourse(const char* a_path)
{
	std::ifstream f(a_path);
	if (!f) {
		std::printf("(no %s: skipping the real course)\n", a_path);
		return;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	Course      c;
	std::string err;
	CHECK(Course::Parse(ss.str(), c, err), "course.txt: %s", err.c_str());
	CHECK(c.holes.size() == 18, "18 holes, got %zu", c.holes.size());
	for (const auto& h : c.holes) {
		Sim s(h);
		CHECK(s.State() == BallState::kRest, "hole %d: no ground at the tee", h.number);
		double     z;
		const Tri* t;
		const Vec2 nearCup = h.cup.XY() + Vec2{ h.cupRadius + 2.0, 0.0 };
		const Vec2 nearCup2 = h.cup.XY() - Vec2{ h.cupRadius + 2.0, 0.0 };
		CHECK(s.Ground(nearCup, h.cup.z + 1.0, z, t) || s.Ground(nearCup2, h.cup.z + 1.0, z, t),
			"hole %d: no green round the cup", h.number);
		const double yaw = h.teeYaw * 3.14159265358979 / 180.0;
		s.Strike({ std::sin(yaw), std::cos(yaw) }, 120.0);
		const auto st = Run(s);
		std::printf("hole %2d par %d %-22s tris %4zu walls %3zu obstacles %zu  tee putt -> state %d at (%.0f %.0f %.0f)\n",
			h.number, h.par, h.name.c_str(), h.tris.size(), h.walls.size(), h.obstacles.size(), int(st), s.Pos().x,
			s.Pos().y, s.Pos().z);
	}
}

int main(int argc, char** argv)
{
	TestRollDistance();
	TestWallBounce();
	TestCup();
	TestSlopes();
	TestRampJump();
	TestWater();
	TestSpinner();
	TestMovingKinds();
	TestLevels();
	TestPredict();
	TestParse();
	TestRealCourse(argc > 1 ? argv[1] : "dist/Data/F4SE/Plugins/fo4minigolf/course.txt");
	if (g_failed) {
		std::printf("%d check(s) failed\n", g_failed);
		return 1;
	}
	std::printf("all passed\n");
	return 0;
}
