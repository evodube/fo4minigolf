#include "sim/Course.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace golf
{
	namespace
	{
		constexpr double kTau = 6.283185307179586;

		bool ParseSurface(const std::string& a_word, Surface& a_out)
		{
			if (a_word == "green") {
				a_out = Surface::kGreen;
			} else if (a_word == "water") {
				a_out = Surface::kWater;
			} else if (a_word == "fast") {
				a_out = Surface::kFast;
			} else if (a_word == "slow") {
				a_out = Surface::kSlow;
			} else {
				return false;
			}
			return true;
		}
	}

	double Vec2::Length() const { return std::sqrt(x * x + y * y); }
	double Vec3::Length() const { return std::sqrt(x * x + y * y + z * z); }

	namespace
	{
		double Cycle(const Obstacle& a_o, double a_time) { return a_o.period == 0.0 ? 0.0 : a_time / a_o.period + a_o.phase; }

		// a portcullis' rise, 0..1: shut 40%, up 10%, open 40%, down 10%
		double LiftFraction(double a_cycle)
		{
			const double c = a_cycle - std::floor(a_cycle);
			if (c < 0.4) {
				return 0.0;
			}
			if (c < 0.5) {
				return (1.0 - std::cos((c - 0.4) / 0.1 * 3.141592653589793)) * 0.5;
			}
			if (c < 0.9) {
				return 1.0;
			}
			return (1.0 + std::cos((c - 0.9) / 0.1 * 3.141592653589793)) * 0.5;
		}

		Vec2 SwingDir(const Obstacle& a_o)
		{
			const double a = a_o.axisDegrees * kTau / 360.0;
			return { -std::sin(a), std::cos(a) };  // level, across the axis
		}
	}

	void Obstacle::Pose(double a_time, double& a_angle, Vec2& a_offset, double& a_lift) const
	{
		a_angle = 0.0;
		a_offset = {};
		a_lift = 0.0;
		const double c = Cycle(*this, a_time);
		switch (motion) {
		case Motion::kSpin:
		case Motion::kWheel:
			a_angle = kTau * c;
			break;
		case Motion::kSwing:
		case Motion::kPendulum:
			a_angle = swingDegrees * (kTau / 360.0) * std::sin(kTau * c);
			break;
		case Motion::kSlide:
			a_offset = slide * std::sin(kTau * c);
			break;
		case Motion::kLift:
			a_lift = height * LiftFraction(c);
			break;
		}
	}

	Vec2 Obstacle::Move(Vec2 a_point, double a_time) const
	{
		double angle, lift;
		Vec2   offset;
		Pose(a_time, angle, offset, lift);
		switch (motion) {
		case Motion::kSpin:
		case Motion::kSwing:
			{
				const double c = std::cos(angle);
				const double s = std::sin(angle);
				const Vec2   d = a_point - pivot;
				return pivot + Vec2{ d.x * c - d.y * s, d.x * s + d.y * c };
			}
		case Motion::kSlide:
			return a_point + offset;
		case Motion::kPendulum:
			// the ball's walls ride along with it: sideways by length * sin(angle). (A positive turn about the
			// axis, right hand rule, swings the bottom towards +SwingDir: axis x down = SwingDir.)
			return a_point + SwingDir(*this) * (length * std::sin(angle));
		default:
			return a_point;  // wheel, lift: the walls don't move, they come and go
		}
	}

	Vec2 Obstacle::Velocity(Vec2 a_pointNow, double a_time) const
	{
		if (period == 0.0) {
			return {};
		}
		const double w = kTau / period;
		const double c = Cycle(*this, a_time);
		auto         perp = [](Vec2 v) { return Vec2{ -v.y, v.x }; };
		switch (motion) {
		case Motion::kSpin:
			return perp(a_pointNow - pivot) * w;
		case Motion::kSwing:
			return perp(a_pointNow - pivot) * (swingDegrees * (kTau / 360.0) * w * std::cos(kTau * c));
		case Motion::kSlide:
			return slide * (w * std::cos(kTau * c));
		case Motion::kPendulum:
			{
				const double amp = swingDegrees * (kTau / 360.0);
				const double angle = amp * std::sin(kTau * c);
				const double rate = amp * w * std::cos(kTau * c);
				return SwingDir(*this) * (length * std::cos(angle) * rate);
			}
		default:
			return {};
		}
	}

	bool Obstacle::Solid(double a_time, double a_ballRadius) const
	{
		if (motion == Motion::kLift) {
			return height * LiftFraction(Cycle(*this, a_time)) < 2.0 * a_ballRadius + 0.5;
		}
		if (motion == Motion::kWheel) {
			const double step = 1.0 / std::max(blades, 1);
			double       c = Cycle(*this, a_time);
			c -= std::floor(c / step) * step;  // the nearest blade's turn past straight down, 0..step
			const double half = halfDegrees / 360.0;
			return c < half || c > step - half;
		}
		return true;
	}

	void Hole::Finish()
	{
		for (auto& t : tris) {
			Vec3 n = (t.b - t.a).Cross(t.c - t.a);
			if (n.z < 0.0) {
				n = n * -1.0;
			}
			const double len = n.Length();
			t.n = len > 0.0 ? n * (1.0 / len) : Vec3{ 0, 0, 1 };
			t.minx = std::min({ t.a.x, t.b.x, t.c.x });
			t.maxx = std::max({ t.a.x, t.b.x, t.c.x });
			t.miny = std::min({ t.a.y, t.b.y, t.c.y });
			t.maxy = std::max({ t.a.y, t.b.y, t.c.y });
		}
		// vertical (or nearly) triangles carry no ground
		std::erase_if(tris, [](const Tri& t) { return t.n.z < 0.05; });

		_cells.clear();
		if (tris.empty()) {
			_nx = _ny = 0;
			return;
		}
		double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
		for (const auto& t : tris) {
			x0 = std::min(x0, t.minx);
			y0 = std::min(y0, t.miny);
			x1 = std::max(x1, t.maxx);
			y1 = std::max(y1, t.maxy);
		}
		_gridX = x0 - 1.0;
		_gridY = y0 - 1.0;
		_nx = static_cast<int>((x1 - _gridX) / kCell) + 2;
		_ny = static_cast<int>((y1 - _gridY) / kCell) + 2;
		_cells.assign(static_cast<std::size_t>(_nx) * _ny, {});
		for (int i = 0; i < static_cast<int>(tris.size()); ++i) {
			const auto& t = tris[i];
			const int   cx0 = static_cast<int>((t.minx - _gridX) / kCell);
			const int   cx1 = static_cast<int>((t.maxx - _gridX) / kCell);
			const int   cy0 = static_cast<int>((t.miny - _gridY) / kCell);
			const int   cy1 = static_cast<int>((t.maxy - _gridY) / kCell);
			for (int cy = cy0; cy <= cy1; ++cy) {
				for (int cx = cx0; cx <= cx1; ++cx) {
					_cells[static_cast<std::size_t>(cy) * _nx + cx].push_back(i);
				}
			}
		}
	}

	const std::vector<int>& Hole::TrisNear(Vec2 a_xy) const
	{
		const int cx = static_cast<int>(std::floor((a_xy.x - _gridX) / kCell));
		const int cy = static_cast<int>(std::floor((a_xy.y - _gridY) / kCell));
		if (cx < 0 || cy < 0 || cx >= _nx || cy >= _ny) {
			return _none;
		}
		return _cells[static_cast<std::size_t>(cy) * _nx + cx];
	}

	bool Course::Parse(const std::string& a_text, Course& a_out, std::string& a_error)
	{
		a_out.holes.clear();
		std::istringstream lines(a_text);
		std::string        line;
		int                lineNo = 0;
		auto               fail = [&](const std::string& a_why) {
			a_error = "line " + std::to_string(lineNo) + ": " + a_why;
			return false;
		};

		while (std::getline(lines, line)) {
			++lineNo;
			if (const auto hash = line.find('#'); hash != std::string::npos) {
				line.erase(hash);
			}
			std::istringstream in(line);
			std::string        kind;
			if (!(in >> kind)) {
				continue;
			}
			if (kind == "course") {
				continue;  // version line
			}
			if (kind == "hole") {
				Hole h;
				if (!(in >> h.number >> h.par)) {
					return fail("hole needs a number and a par");
				}
				std::getline(in >> std::ws, h.name);
				while (!h.name.empty() && std::isspace(static_cast<unsigned char>(h.name.back()))) {
					h.name.pop_back();
				}
				a_out.holes.push_back(std::move(h));
				continue;
			}
			if (a_out.holes.empty()) {
				return fail("'" + kind + "' before the first hole");
			}
			Hole& h = a_out.holes.back();
			if (kind == "tee") {
				if (!(in >> h.tee.x >> h.tee.y >> h.tee.z >> h.teeYaw)) {
					return fail("tee needs x y z yaw");
				}
			} else if (kind == "cup") {
				if (!(in >> h.cup.x >> h.cup.y >> h.cup.z >> h.cupRadius)) {
					return fail("cup needs x y z radius");
				}
			} else if (kind == "bounds") {
				if (!(in >> h.minx >> h.miny >> h.maxx >> h.maxy >> h.minz)) {
					return fail("bounds needs minx miny maxx maxy minz");
				}
			} else if (kind == "tri") {
				Tri         t;
				std::string surface;
				if (!(in >> surface) || !ParseSurface(surface, t.surface)) {
					return fail("unknown surface '" + surface + "'");
				}
				if (!(in >> t.a.x >> t.a.y >> t.a.z >> t.b.x >> t.b.y >> t.b.z >> t.c.x >> t.c.y >> t.c.z)) {
					return fail("tri needs 9 numbers");
				}
				h.tris.push_back(t);
			} else if (kind == "wall" || kind == "owall") {
				Wall w;
				if (!(in >> w.a.x >> w.a.y >> w.b.x >> w.b.y >> w.zmin >> w.zmax >> w.restitution)) {
					return fail(kind + " needs ax ay bx by zmin zmax restitution");
				}
				if (kind == "wall") {
					h.walls.push_back(w);
				} else {
					if (h.obstacles.empty()) {
						return fail("owall before any obstacle");
					}
					h.obstacles.back().walls.push_back(w);
				}
			} else if (kind == "ovisual") {
				if (h.obstacles.empty()) {
					return fail("ovisual before any obstacle");
				}
				auto& o = h.obstacles.back();
				if (!(in >> o.origin.x >> o.origin.y >> o.origin.z >> o.yawDegrees)) {
					return fail("ovisual needs x y z yaw");
				}
			} else if (kind == "obstacle") {
				Obstacle    o;
				std::string motion;
				if (!(in >> o.name >> motion)) {
					return fail("obstacle needs a name and a motion");
				}
				bool ok = false;
				if (motion == "spin") {
					o.motion = Obstacle::Motion::kSpin;
					ok = static_cast<bool>(in >> o.pivot.x >> o.pivot.y >> o.period >> o.phase);
				} else if (motion == "slide") {
					o.motion = Obstacle::Motion::kSlide;
					ok = static_cast<bool>(in >> o.slide.x >> o.slide.y >> o.period >> o.phase);
				} else if (motion == "swing") {
					o.motion = Obstacle::Motion::kSwing;
					ok = static_cast<bool>(in >> o.pivot.x >> o.pivot.y >> o.swingDegrees >> o.period >> o.phase);
				} else if (motion == "wheel") {
					o.motion = Obstacle::Motion::kWheel;
					ok = static_cast<bool>(in >> o.pivot.x >> o.pivot.y >> o.pivotZ >> o.axisDegrees >> o.period >> o.phase >>
					                       o.blades >> o.halfDegrees);
				} else if (motion == "pendulum") {
					o.motion = Obstacle::Motion::kPendulum;
					ok = static_cast<bool>(in >> o.pivot.x >> o.pivot.y >> o.pivotZ >> o.axisDegrees >> o.length >>
					                       o.swingDegrees >> o.period >> o.phase);
				} else if (motion == "lift") {
					o.motion = Obstacle::Motion::kLift;
					ok = static_cast<bool>(in >> o.height >> o.period >> o.phase);
				} else {
					return fail("unknown motion '" + motion + "'");
				}
				if (!ok) {
					return fail("obstacle " + o.name + ": missing numbers");
				}
				h.obstacles.push_back(std::move(o));
			} else {
				return fail("unknown record '" + kind + "'");
			}
		}

		for (auto& h : a_out.holes) {
			if (h.tris.empty()) {
				a_error = "hole " + std::to_string(h.number) + " has no ground";
				return false;
			}
			h.Finish();
		}
		if (a_out.holes.empty()) {
			a_error = "no holes";
			return false;
		}
		return true;
	}
}
