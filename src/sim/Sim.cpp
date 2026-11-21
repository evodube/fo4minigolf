#include "sim/Sim.h"

#include <algorithm>
#include <cmath>

namespace golf
{
	namespace
	{
		constexpr double kTau = 6.283185307179586;
		constexpr double kMaxStep = 1.0 / 480.0;
		constexpr double kRestStep = 1.0 / 120.0;
		constexpr double kMaxTravelPerStep = 0.2;  // in ball radii
		constexpr double kSettleSpeed = 30.0;      // a landing that would bounce up slower than this rolls instead
		constexpr double kCupLevel = 3.0;          // ground within this height of the cup's rim is cut by the cup

		Vec2 Perp(Vec2 a_v) { return { -a_v.y, a_v.x }; }

		bool InTri(const Tri& a_t, Vec2 a_p)
		{
			const double eps = 1e-7;
			if (a_p.x < a_t.minx - eps || a_p.x > a_t.maxx + eps || a_p.y < a_t.miny - eps || a_p.y > a_t.maxy + eps) {
				return false;
			}
			const Vec2   a = a_t.a.XY(), b = a_t.b.XY(), c = a_t.c.XY();
			const double d1 = (b - a).Cross(a_p - a);
			const double d2 = (c - b).Cross(a_p - b);
			const double d3 = (a - c).Cross(a_p - c);
			const bool   neg = d1 < -eps || d2 < -eps || d3 < -eps;
			const bool   pos = d1 > eps || d2 > eps || d3 > eps;
			return !(neg && pos);
		}

		double PlaneZ(const Tri& a_t, Vec2 a_p)
		{
			return a_t.a.z - (a_t.n.x * (a_p.x - a_t.a.x) + a_t.n.y * (a_p.y - a_t.a.y)) / a_t.n.z;
		}

		Vec3 OntoPlane(Vec3 a_v, const Vec3& a_n) { return a_v - a_n * a_v.Dot(a_n); }
	}

	Quat Quat::operator*(const Quat& o) const
	{
		return {
			w * o.w - x * o.x - y * o.y - z * o.z,
			w * o.x + x * o.w + y * o.z - z * o.y,
			w * o.y - x * o.z + y * o.w + z * o.x,
			w * o.z + x * o.y - y * o.x + z * o.w
		};
	}

	void Quat::Normalize()
	{
		const double n = std::sqrt(w * w + x * x + y * y + z * z);
		if (n > 0.0) {
			w /= n;
			x /= n;
			y /= n;
			z /= n;
		} else {
			*this = {};
		}
	}

	void Quat::ToMatrix(double m[3][3]) const
	{
		m[0][0] = 1 - 2 * (y * y + z * z);
		m[0][1] = 2 * (x * y - z * w);
		m[0][2] = 2 * (x * z + y * w);
		m[1][0] = 2 * (x * y + z * w);
		m[1][1] = 1 - 2 * (x * x + z * z);
		m[1][2] = 2 * (y * z - x * w);
		m[2][0] = 2 * (x * z - y * w);
		m[2][1] = 2 * (y * z + x * w);
		m[2][2] = 1 - 2 * (x * x + y * y);
	}

	Sim::Sim(const Hole& a_hole, const Spec& a_spec) :
		_hole(&a_hole),
		_spec(a_spec)
	{
		PlaceOnTee();
	}

	bool Sim::OverCup(Vec2 a_xy) const
	{
		return (a_xy - _hole->cup.XY()).LengthSq() < _hole->cupRadius * _hole->cupRadius;
	}

	bool Sim::Ground(Vec2 a_xy, double a_zTop, double& a_z, const Tri*& a_tri) const
	{
		const bool overCup = OverCup(a_xy);
		bool       found = false;
		for (const int i : _hole->TrisNear(a_xy)) {
			const Tri& t = _hole->tris[i];
			if (!InTri(t, a_xy)) {
				continue;
			}
			const double z = PlaneZ(t, a_xy);
			if (z > a_zTop || (found && z <= a_z)) {
				continue;
			}
			if (overCup && std::abs(z - _hole->cup.z) < kCupLevel) {
				continue;  // the hole itself
			}
			a_z = z;
			a_tri = &t;
			found = true;
		}
		return found;
	}

	double Sim::CentreZ(double a_surface, const Tri* a_tri) const
	{
		return a_surface + _spec.ballRadius / a_tri->n.z;
	}

	double Sim::Resistance(const Tri* a_tri) const
	{
		switch (a_tri ? a_tri->surface : Surface::kGreen) {
		case Surface::kFast:
			return _spec.rollDecel * _spec.fastFactor;
		case Surface::kSlow:
			return _spec.rollDecel * _spec.slowFactor;
		case Surface::kSand:
			return _spec.rollDecel * _spec.sandFactor;
		default:
			return _spec.rollDecel;
		}
	}

	bool Sim::Place(Vec2 a_xy, double a_zHint)
	{
		double     z = 0.0;
		const Tri* tri = nullptr;
		if (!Ground(a_xy, a_zHint, z, tri)) {
			return false;
		}
		_pos = { a_xy.x, a_xy.y, CentreZ(z, tri) };
		_vel = {};
		_ground = tri;
		_state = BallState::kRest;
		_shotTime = 0.0;
		return true;
	}

	void Sim::Strike(Vec2 a_dir, double a_speed)
	{
		const double len = a_dir.Length();
		if (len <= 0.0 || !_ground) {
			return;
		}
		a_speed = std::clamp(a_speed, 0.0, _spec.maxSpeed);
		const Vec3 d{ a_dir.x / len, a_dir.y / len, 0.0 };
		Vec3       v = OntoPlane(d, _ground->n);
		const double vl = v.Length();
		_vel = vl > 0.0 ? v * (a_speed / vl) : Vec3{};
		_state = BallState::kRolling;
		_shotTime = 0.0;
	}

	void Sim::ObstaclePose(int a_index, double& a_angle, Vec2& a_offset, double& a_lift) const
	{
		_hole->obstacles[a_index].Pose(_time, a_angle, a_offset, a_lift);
	}

	std::vector<Event> Sim::Step(double a_dt)
	{
		// Whole substeps only, their size set by the ball alone (never cut short to land on a_dt): the same shot comes
		// out the same however the time is handed over - the game's frames, or a settler's planning in 1/100 s steps.
		// (Cut to fit the frames, the substeps differed, and a long banked putt planned on a copy went elsewhere in
		// the game.) The ball's time can run up to one substep past the caller's; the next call takes it back up.
		std::vector<Event> events;
		_target += a_dt;
		while (_time < _target - 1e-12) {
			if (_state == BallState::kInCup || _state == BallState::kWater || _state == BallState::kOut) {
				_time = _target;  // the ball is done; the obstacles keep going
				break;
			}
			double h;
			if (_state == BallState::kRest) {
				h = kRestStep;
			} else {
				const double speed = std::max(_vel.Length(), 1.0);
				h = std::min(kMaxStep, kMaxTravelPerStep * _spec.ballRadius / speed);
			}
			SubStep(h, events);
		}
		return events;
	}

	void Sim::SubStep(double a_h, std::vector<Event>& a_events)
	{
		_time += a_h;
		if (_state == BallState::kRolling) {
			Roll(a_h, a_events);
		} else if (_state == BallState::kAir) {
			Fly(a_h, a_events);
		}
		CollideWalls(a_events);
		if (_state == BallState::kRolling || _state == BallState::kAir) {
			CollideRim(a_events);
			_shotTime += a_h;
		}
		CheckFinish(a_events);
	}

	void Sim::Roll(double a_h, std::vector<Event>& a_events)
	{
		const Vec3& n = _ground->n;
		// gravity along the slope, for a ball rolling without slipping
		const Vec3   along = Vec3{ 0.0, 0.0, -_spec.gravity } + n * (_spec.gravity * n.z);
		const Vec3   accel = along * (5.0 / 7.0);
		const double decel = Resistance(_ground);

		Vec3         v = OntoPlane(_vel, n);
		const double speed = v.Length();
		if (speed < _spec.stopSpeed && accel.Length() <= decel) {
			_vel = {};
			_state = BallState::kRest;
			a_events.push_back({ Event::Type::kRest, -1, 0.0, _pos });
			return;
		}
		if (speed > 0.0) {
			const double drop = decel * a_h;
			v = speed <= drop ? Vec3{} : v * ((speed - drop) / speed);
		}
		v += accel * a_h;

		const double surfaceNow = _pos.z - _spec.ballRadius / n.z;
		Vec3         next = _pos + v * a_h;
		double       z = 0.0;
		const Tri*   tri = nullptr;
		const double zTop = surfaceNow + _spec.stepUp * _spec.ballRadius + std::max(0.0, v.z * a_h) +
		                    v.Length() * a_h * 0.5;
		if (!Ground(next.XY(), zTop, z, tri)) {
			_vel = v;
			_pos = next;
			_state = BallState::kAir;  // ran off an edge, or over the cup
			return;
		}
		const double target = CentreZ(z, tri);
		if (next.z > target + 0.25 * _spec.ballRadius) {
			_vel = v;
			_pos = next;
			_state = BallState::kAir;  // the ground falls away (a step down, the end of a ramp)
			return;
		}
		if (tri != _ground) {
			const double away = v.Dot(tri->n);
			if (away > _spec.launchSpeed) {
				_vel = v;
				_pos = { next.x, next.y, std::max(next.z, target) };
				_state = BallState::kAir;  // over a crest fast enough to fly
				return;
			}
			const double before = v.Length();
			Vec3         p = OntoPlane(v, tri->n);
			const double pl = p.Length();
			v = pl > 1e-9 ? p * (before * 0.995 / pl) : Vec3{};
			_ground = tri;
		}
		_vel = v;
		_pos = { next.x, next.y, target };
		Spin(a_h);
		if (tri->surface == Surface::kWater) {
			_state = BallState::kWater;
			a_events.push_back({ Event::Type::kWater, -1, v.Length(), _pos });
		}
	}

	void Sim::Fly(double a_h, std::vector<Event>& a_events)
	{
		_vel.z -= _spec.gravity * a_h;
		const Vec3 next = _pos + _vel * a_h;
		double     z = 0.0;
		const Tri* tri = nullptr;
		if (Ground(next.XY(), _pos.z, z, tri)) {
			const double target = CentreZ(z, tri);
			if (next.z <= target) {
				_pos = { next.x, next.y, target };
				const Vec3&  n = tri->n;
				const double vn = _vel.Dot(n);
				if (vn < 0.0) {
					a_events.push_back({ Event::Type::kLand, -1, -vn, _pos });
					if (tri->surface == Surface::kWater) {
						_state = BallState::kWater;
						a_events.push_back({ Event::Type::kWater, -1, -vn, _pos });
						return;
					}
					const bool   sand = tri->surface == Surface::kSand;
					const double bounce = sand ? 0.0 : -vn * _spec.landRestitution;
					Vec3         tangent = OntoPlane(_vel, n) * (sand ? 0.35 : 0.97);
					if (bounce < kSettleSpeed) {
						_vel = tangent;
						_ground = tri;
						_state = BallState::kRolling;
					} else {
						_vel = tangent + n * bounce;
					}
				}
				return;
			}
		}
		_pos = next;
	}

	void Sim::CollideWalls(std::vector<Event>& a_events)
	{
		for (const auto& w : _hole->walls) {
			CollideWall(w, w.a, w.b, -1, a_events);
		}
		for (int i = 0; i < static_cast<int>(_hole->obstacles.size()); ++i) {
			const auto& o = _hole->obstacles[i];
			if (!o.Solid(_time, _spec.ballRadius)) {
				continue;
			}
			for (const auto& w : o.walls) {
				CollideWall(w, o.Move(w.a, _time), o.Move(w.b, _time), i, a_events);
			}
		}
	}

	bool Sim::CollideWall(const Wall& a_wall, Vec2 a_a, Vec2 a_b, int a_obstacle, std::vector<Event>& a_events)
	{
		const double r = _spec.ballRadius;
		if (_pos.z - r >= a_wall.zmax || _pos.z + r <= a_wall.zmin) {
			return false;
		}
		const Vec2   c = _pos.XY();
		const Vec2   ab = a_b - a_a;
		const double len2 = ab.LengthSq();
		double       t = len2 > 0.0 ? std::clamp((c - a_a).Dot(ab) / len2, 0.0, 1.0) : 0.0;
		const Vec2   q = a_a + ab * t;
		Vec2         d = c - q;
		double       dist = d.Length();
		if (dist >= r) {
			return false;
		}
		Vec2 nrm;
		if (dist > 1e-9) {
			nrm = d * (1.0 / dist);
		} else {
			nrm = Perp(ab) * (1.0 / std::sqrt(std::max(len2, 1e-12)));
		}

		const Vec2   wallVel = a_obstacle >= 0 ? _hole->obstacles[a_obstacle].Velocity(q, _time) : Vec2{};
		Vec2         v{ _vel.x, _vel.y };
		const double vn = (v - wallVel).Dot(nrm);
		if (vn < 0.0) {
			v = v - nrm * ((1.0 + a_wall.restitution) * vn);
			_vel.x = v.x;
			_vel.y = v.y;
			if (-vn > 3.0) {
				a_events.push_back({ Event::Type::kWall, a_obstacle, -vn, _pos });
			}
			if (_state == BallState::kRest) {
				_state = _ground ? BallState::kRolling : BallState::kAir;
				_shotTime = 0.0;
			}
		}
		const Vec2 out = q + nrm * r;
		_pos.x = out.x;
		_pos.y = out.y;
		return true;
	}

	void Sim::CollideRim(std::vector<Event>& a_events)
	{
		const double r = _spec.ballRadius;
		const double R = _hole->cupRadius;
		const Vec3&  cup = _hole->cup;
		Vec2         d = _pos.XY() - cup.XY();
		const double dist = d.Length();
		if (dist > R + r || _pos.z > cup.z + 2.0 * r || _pos.z < cup.z - 4.0 * r || dist < 1e-6) {
			return;
		}
		const Vec2 dir = d * (1.0 / dist);

		// the rim: a circle at the cup's height. The ball can touch it from above, from inside or from outside.
		const Vec3   rim{ cup.x + dir.x * R, cup.y + dir.y * R, cup.z };
		const Vec3   off = _pos - rim;
		const double od = off.Length();
		if (od < r && od > 1e-9) {
			const Vec3   nrm = off * (1.0 / od);
			const double vn = _vel.Dot(nrm);
			if (vn < 0.0) {
				_vel = _vel - nrm * ((1.0 + _spec.rimRestitution) * vn);
				a_events.push_back({ Event::Type::kRim, -1, -vn, _pos });
			}
			_pos = rim + nrm * r;
			if (_state == BallState::kRolling && dist < R) {
				_state = BallState::kAir;
			}
		}

		// the cup's wall, once the ball is down in it
		if (dist < R && _pos.z < cup.z && dist > R - r) {
			const Vec2 inward = dir * -1.0;
			_pos.x = cup.x + dir.x * (R - r);
			_pos.y = cup.y + dir.y * (R - r);
			const double vn = Vec2{ _vel.x, _vel.y }.Dot(inward);
			if (vn < 0.0) {
				_vel.x -= inward.x * vn * 1.3;
				_vel.y -= inward.y * vn * 1.3;
			}
		}
	}

	void Sim::CheckFinish(std::vector<Event>& a_events)
	{
		const Hole& h = *_hole;
		if (OverCup(_pos.XY()) && _pos.z < h.cup.z) {
			_state = BallState::kInCup;
			_vel = {};
			_pos = { h.cup.x, h.cup.y, h.cup.z - 2.0 * _spec.ballRadius };
			a_events.push_back({ Event::Type::kCup, -1, 0.0, _pos });
			return;
		}
		if (_pos.x < h.minx || _pos.x > h.maxx || _pos.y < h.miny || _pos.y > h.maxy || _pos.z < h.minz) {
			_state = BallState::kOut;
			_vel = {};
			a_events.push_back({ Event::Type::kOut, -1, 0.0, _pos });
			return;
		}
		if (Moving() && _shotTime > _spec.maxShotTime) {
			// stuck somewhere (rocking in a dip, wedged against an obstacle): settle where it is
			double     z = 0.0;
			const Tri* tri = nullptr;
			if (Ground(_pos.XY(), _pos.z, z, tri)) {
				_ground = tri;
				_pos.z = CentreZ(z, tri);
				_vel = {};
				_state = BallState::kRest;
				a_events.push_back({ Event::Type::kRest, -1, 0.0, _pos });
			} else {
				_state = BallState::kOut;
				_vel = {};
				a_events.push_back({ Event::Type::kOut, -1, 0.0, _pos });
			}
		}
	}

	void Sim::Spin(double a_h)
	{
		if (!_ground) {
			return;
		}
		// rolling without slipping: w = n x v / r
		const Vec3   w = _ground->n.Cross(_vel) * (1.0 / _spec.ballRadius);
		const double rate = w.Length();
		if (rate < 1e-9) {
			return;
		}
		const double angle = rate * a_h;
		const double s = std::sin(angle * 0.5) / rate;
		const Quat   dq{ std::cos(angle * 0.5), w.x * s, w.y * s, w.z * s };
		_rot = dq * _rot;
		_rot.Normalize();
	}

	std::vector<Vec3> Sim::Predict(Vec2 a_dir, double a_speed, double a_maxLength, double a_spacing, bool a_stopAtTurn) const
	{
		Sim copy = *this;
		copy.Strike(a_dir, a_speed);
		std::vector<Vec3> points;
		points.push_back(copy._pos);
		double travelled = 0.0;
		double sinceDot = 0.0;
		Vec3   last = copy._pos;
		for (int i = 0; i < 1200 && copy.Moving(); ++i) {  // 10 s at most
			const Vec2 before = copy._vel.XY();
			copy.Step(1.0 / 120.0);
			if (a_stopAtTurn) {
				const Vec2   now = copy._vel.XY();
				const double slow = 20.0;  // (a turn this slow is the slope's doing; a bounce off a wall is fast)
				if (before.Dot(now) < 0.0 && before.Length() < slow) {
					points.push_back(copy._pos);
					return points;
				}
			}
			const double step = (copy._pos - last).Length();
			last = copy._pos;
			travelled += step;
			sinceDot += step;
			if (sinceDot >= a_spacing) {
				points.push_back(copy._pos);
				sinceDot = 0.0;
			}
			if (travelled >= a_maxLength) {
				break;
			}
		}
		if (points.size() == 1 || sinceDot > a_spacing * 0.3) {
			points.push_back(copy._pos);
		}
		return points;
	}
}
