#include "sim/Bot.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <thread>

namespace golf
{
	namespace
	{
		bool SegmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
		{
			const double d1 = (b - a).Cross(c - a);
			const double d2 = (b - a).Cross(d - a);
			const double d3 = (d - c).Cross(a - c);
			const double d4 = (d - c).Cross(b - c);
			return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
		}

		std::uint32_t Next(std::uint32_t& s)
		{
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			return s;
		}
	}

	Bot::Bot(const Hole& a_hole, const Spec& a_spec) :
		_hole(&a_hole),
		_spec(a_spec)
	{
		const Hole& h = a_hole;
		double      x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
		for (const auto& t : h.tris) {
			x0 = std::min(x0, t.minx);
			y0 = std::min(y0, t.miny);
			x1 = std::max(x1, t.maxx);
			y1 = std::max(y1, t.maxy);
		}
		_x0 = x0;
		_y0 = y0;
		_nx = static_cast<int>((x1 - x0) / kCell) + 1;
		_ny = static_cast<int>((y1 - y0) / kCell) + 1;
		const std::size_t n = static_cast<std::size_t>(_nx) * _ny;
		_dist.assign(n, -1.0);
		_floor.assign(n, 0.0);
		std::vector<std::uint8_t> water(n, 0);
		std::vector<std::uint8_t> ground(n, 0);

		Sim probe(h, a_spec);
		auto centre = [&](int i, int j) { return Vec2{ _x0 + (i + 0.5) * kCell, _y0 + (j + 0.5) * kCell }; };
		for (int j = 0; j < _ny; ++j) {
			for (int i = 0; i < _nx; ++i) {
				double     z;
				const Tri* t;
				if (probe.Ground(centre(i, j), 1e9, z, t)) {
					const std::size_t k = static_cast<std::size_t>(j) * _nx + i;
					ground[k] = 1;
					_floor[k] = z;
					water[k] = t->surface == Surface::kWater;
				}
			}
		}

		auto blocked = [&](Vec2 p, Vec2 q, double zp, double zq) {
			for (const auto& w : h.walls) {
				if (w.zmax < std::min(zp, zq) + 1.0 || w.zmin > std::max(zp, zq) + 3.0) {
					continue;
				}
				if (SegmentsCross(p, q, w.a, w.b)) {
					return true;
				}
			}
			return false;
		};

		// Dijkstra from the cup, the ball's way reversed: from cell q (nearer the cup) back to p, allowed if a ball
		// could roll p -> q (not up a step)
		using Item = std::pair<double, std::size_t>;
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
		const int ci = static_cast<int>((h.cup.x - _x0) / kCell);
		const int cj = static_cast<int>((h.cup.y - _y0) / kCell);
		for (int dj = -1; dj <= 1; ++dj) {
			for (int di = -1; di <= 1; ++di) {
				const int i = ci + di, j = cj + dj;
				if (i < 0 || j < 0 || i >= _nx || j >= _ny) {
					continue;
				}
				const std::size_t k = static_cast<std::size_t>(j) * _nx + i;
				const double      d = (centre(i, j) - h.cup.XY()).Length();
				_dist[k] = d;
				open.push({ d, k });
			}
		}
		while (!open.empty()) {
			const auto [d, k] = open.top();
			open.pop();
			if (d > _dist[k]) {
				continue;
			}
			const int  qi = static_cast<int>(k % _nx), qj = static_cast<int>(k / _nx);
			const Vec2 q = centre(qi, qj);
			for (int dj = -1; dj <= 1; ++dj) {
				for (int di = -1; di <= 1; ++di) {
					if (!di && !dj) {
						continue;
					}
					const int pi = qi + di, pj = qj + dj;
					if (pi < 0 || pj < 0 || pi >= _nx || pj >= _ny) {
						continue;
					}
					const std::size_t m = static_cast<std::size_t>(pj) * _nx + pi;
					if (!ground[m]) {
						continue;
					}
					if (_floor[k] - _floor[m] > 6.0) {
						continue;  // a step up the ball can't climb
					}
					const Vec2 p = centre(pi, pj);
					if (blocked(p, q, _floor[m], _floor[k])) {
						continue;
					}
					const double step = (p - q).Length() * (water[m] ? 6.0 : 1.0);
					const double nd = d + step;
					if (_dist[m] < 0.0 || nd < _dist[m]) {
						_dist[m] = nd;
						open.push({ nd, m });
					}
				}
			}
		}
	}

	double Bot::Distance(Vec3 a_pos) const
	{
		const int i = static_cast<int>(std::floor((a_pos.x - _x0) / kCell));
		const int j = static_cast<int>(std::floor((a_pos.y - _y0) / kCell));
		double    best = 1e9;
		for (int r = 0; r <= 2 && best >= 1e9; ++r) {
			for (int dj = -r; dj <= r; ++dj) {
				for (int di = -r; di <= r; ++di) {
					const int x = i + di, y = j + dj;
					if (x < 0 || y < 0 || x >= _nx || y >= _ny) {
						continue;
					}
					const double d = _dist[static_cast<std::size_t>(y) * _nx + x];
					if (d < 0.0) {
						continue;
					}
					const Vec2 c{ _x0 + (x + 0.5) * kCell, _y0 + (y + 0.5) * kCell };
					best = std::min(best, d + (c - a_pos.XY()).Length());
				}
			}
		}
		return best;
	}

	Shot Bot::Try(const Sim& a_sim, Vec2 a_dir, double a_speed, double a_wait) const
	{
		Sim copy = a_sim;
		if (a_wait > 0.0) {
			copy.Step(a_wait);
		}
		copy.Strike(a_dir, a_speed);
		for (int i = 0; i < 1500 && copy.Moving(); ++i) {
			copy.Step(1.0 / 100.0);
		}
		Shot s{ a_dir, a_speed, a_wait, 0.0, copy.State(), copy.Pos() };
		switch (copy.State()) {
		case BallState::kInCup:
			s.score = -1e6 + a_wait;
			break;
		case BallState::kWater:
		case BallState::kOut:
			s.score = 1e7;
			break;
		default:
			s.score = Distance(copy.Pos()) + a_wait * 2.0;
			break;
		}
		return s;
	}

	Vec2 Bot::Toward(Vec3 a_pos) const
	{
		// the lowest of the field round the ball, a little way out (past the cell it's in)
		Vec2   best{ 1.0, 0.0 };
		double low = 1e18;
		for (const double r : { 10.0, 18.0 }) {
			for (int k = 0; k < 48; ++k) {
				const double a = k / 48.0 * 6.283185307179586;
				const Vec2   d{ std::cos(a), std::sin(a) };
				const double v = Distance({ a_pos.x + d.x * r, a_pos.y + d.y * r, a_pos.z });
				if (v < low) {
					low = v;
					best = d;
				}
			}
			if (low < 1e9) {
				break;
			}
		}
		return best;
	}

	Shot Bot::Settler(const Sim& a_sim, double a_skill, std::uint32_t a_seed, int a_threads) const
	{
		const Vec3   from = a_sim.Pos();
		const double before = Distance(from);
		const int    dirs = _hole->obstacles.empty() ? 48 : 36;
		// the cup's way: within about 100 degrees of the line along the ground to it (banks off the rails still in)
		Shot best = Choose(a_sim, dirs, 10, a_seed, a_threads, -0.17);
		if (best.end != BallState::kInCup && !(best.score < before)) {
			best = Choose(a_sim, dirs, 10, a_seed + 7, a_threads);  // (nothing that way gains: anything that does)
		}
		if (best.end != BallState::kInCup && !(best.score < before)) {
			// still nothing: a firm putt straight down the line to the cup
			const Vec2 to = Toward(from);
			best = Try(a_sim, to, std::clamp(before * 1.4, 60.0, _spec.maxSpeed * 0.7), 0.0);
		}
		// their miss: off line and weight by skill, kept only if it still gains ground and stays out of trouble
		a_skill = std::clamp(a_skill, 0.0, 1.0);
		std::uint32_t rng = a_seed * 747796405u + 2891336453u;
		const auto    gauss = [&]() {
            double s = 0.0;  // (near enough normal: the sum of four uniforms)
            for (int i = 0; i < 4; ++i) {
                s += (Next(rng) % 10001) / 10000.0;
            }
            return (s - 2.0) * 1.732;
		};
		const double aimSigma = (0.5 + (1.0 - a_skill) * 10.0) * 0.017453292519943295;
		const double speedSigma = 0.04 + (1.0 - a_skill) * 0.35;
		const double gain = before - (best.end == BallState::kInCup ? 0.0 : Distance(best.rest));
		for (int tries = 0; tries < 10; ++tries) {
			const double da = gauss() * aimSigma;
			const double ds = std::clamp(1.0 + gauss() * speedSigma, 0.6, 1.4);
			const double c = std::cos(da), s = std::sin(da);
			const Vec2   dir{ best.dir.x * c - best.dir.y * s, best.dir.x * s + best.dir.y * c };
			const Shot   t = Try(a_sim, dir, std::min(best.speed * ds, _spec.maxSpeed), best.wait);
			if (t.end == BallState::kWater || t.end == BallState::kOut) {
				continue;
			}
			if (t.end == BallState::kInCup || Distance(t.rest) < before - std::max(2.0, gain * 0.15)) {
				return t;  // a miss, but a fair one: still nearer the cup
			}
		}
		return best;
	}

	Shot Bot::Choose(const Sim& a_sim, int a_dirs, int a_speeds, std::uint32_t a_seed, int a_threads, double a_minDot) const
	{
		const Vec2 toward = a_minDot > -1.5 ? Toward(a_sim.Pos()) : Vec2{ 1.0, 0.0 };
		std::uint32_t rng = a_seed * 2654435761u + 12345u;
		const double  jitter = (Next(rng) % 1000) / 1000.0;
		std::vector<double> waits{ 0.0 };
		if (!_hole->obstacles.empty()) {
			waits = { 0.0, 0.6, 1.3, 2.1, 3.0 };
		}
		Shot best;
		const int threads = std::clamp(a_threads, 1, a_dirs);
		for (const double wait : waits) {
			// each thread takes every threads'th direction; the best of their bests
			std::vector<Shot> bests(static_cast<std::size_t>(threads));
			const auto        run = [&](int a_first) {
                Shot& mine = bests[static_cast<std::size_t>(a_first)];
                for (int d = a_first; d < a_dirs; d += threads) {
                    const double a = (d + jitter) / a_dirs * 6.283185307179586;
                    const Vec2   dir{ std::cos(a), std::sin(a) };
                    if (dir.Dot(toward) < a_minDot) {
                        continue;
                    }
                    for (int k = 0; k < a_speeds; ++k) {
                        // speeds spread geometrically, 20 to full
                        const double speed = 20.0 * std::pow(_spec.maxSpeed / 20.0, k / double(a_speeds - 1));
                        Shot         s = Try(a_sim, dir, speed, wait);
                        if (s.score < mine.score) {
                            mine = s;
                        }
                    }
                }
			};
			if (threads == 1) {
				run(0);
			} else {
				std::vector<std::thread> pool;
				for (int i = 1; i < threads; ++i) {
					pool.emplace_back(run, i);
				}
				run(0);
				for (auto& th : pool) {
					th.join();
				}
			}
			for (const auto& s : bests) {
				if (s.score < best.score) {
					best = s;
				}
			}
			if (best.end == BallState::kInCup) {
				break;
			}
		}
		// refine round the best
		for (int r = 0; r < 40 && best.end != BallState::kInCup; ++r) {
			const double da = ((Next(rng) % 2001) / 1000.0 - 1.0) * 0.06;
			const double ds = 1.0 + ((Next(rng) % 2001) / 1000.0 - 1.0) * 0.12;
			const double c = std::cos(da), s = std::sin(da);
			const Vec2   dir{ best.dir.x * c - best.dir.y * s, best.dir.x * s + best.dir.y * c };
			if (best.speed <= 0.0 || dir.Dot(toward) < a_minDot) {
				continue;
			}
			Shot         t = Try(a_sim, dir, std::min(best.speed * ds, _spec.maxSpeed), best.wait);
			if (t.score < best.score) {
				best = t;
			}
		}
		return best;
	}
}
