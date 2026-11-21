#pragma once

// The course: 18 holes as the plugin's physics sees them. Plain C++ with no game dependencies (tests/sim_test.cpp).
//
// Course space: the course object's own space, in game units (about 70 per metre), z up. Everything here is made by
// tools/course/export_course.py from the Blender scene, so the physics always matches the meshes.
//
// File format (Data/F4SE/Plugins/fo4minigolf/course.txt), one record per line, '#' starts a comment:
//
//     hole <number> <par> <name...>          starts a hole; everything below belongs to it until the next "hole"
//     tee <x> <y> <z> <yaw degrees>          where the ball is placed (z = the surface) and the way the camera faces
//     cup <x> <y> <z> <radius>               the cup: its centre on the surface and its radius
//     bounds <minx> <miny> <maxx> <maxy> <minz>   leaving this box, or falling below minz, is out of bounds
//     tri <surface> x1 y1 z1 x2 y2 z2 x3 y3 z3     a piece of ground. surface: green, water, fast, slow
//     wall <ax> <ay> <bx> <by> <zmin> <zmax> <restitution>   a vertical wall: a segment extruded from zmin to zmax
//     obstacle <name> spin <cx> <cy> <period s> <phase 0..1>          a moving obstacle turning round (cx, cy)
//     obstacle <name> slide <dx> <dy> <period s> <phase 0..1>         one sliding back and forth by +-(dx, dy)
//     obstacle <name> swing <cx> <cy> <degrees> <period s> <phase>    one swinging +-degrees round (cx, cy)
//     obstacle <name> wheel <cx> <cy> <cz> <axis degrees> <period s> <phase> <blades> <half width degrees>
//                                            a windmill: blades turning round a level axis through (cx, cy, cz);
//                                            its walls are there while a blade points down (within +-half width)
//     obstacle <name> pendulum <cx> <cy> <cz> <axis degrees> <length> <degrees> <period s> <phase>
//                                            a wrecking ball on a rope from (cx, cy, cz), swinging +-degrees
//                                            across the level axis; its walls move with the ball
//     obstacle <name> lift <height> <period s> <phase>
//                                            a portcullis: shut for 40% of the period, rises, open for 40%,
//                                            drops; its walls are there while it's lower than a ball
//     owall <ax> <ay> <bx> <by> <zmin> <zmax> <restitution>  a wall of the last obstacle, in course space at time 0
//     ovisual <x> <y> <z> <yaw degrees>      where the last obstacle's mesh sits at time 0 (its nif's origin) and
//                                            its turn about z (maths angle); only the game uses this
//
// Angles in the file are maths angles: degrees anticlockwise from +x, seen from above. A spinning obstacle turns
// anticlockwise once per period; a negative period turns it clockwise. Phases are fractions of a period.

#include <cstdint>
#include <string>
#include <vector>

namespace golf
{
	struct Vec2
	{
		double x{ 0.0 };
		double y{ 0.0 };

		Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
		Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
		Vec2 operator*(double s) const { return { x * s, y * s }; }
		Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
		double Dot(const Vec2& o) const { return x * o.x + y * o.y; }
		double Cross(const Vec2& o) const { return x * o.y - y * o.x; }
		double Length() const;
		double LengthSq() const { return x * x + y * y; }
	};

	struct Vec3
	{
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };

		Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		Vec3 operator*(double s) const { return { x * s, y * s, z * s }; }
		Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		double Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
		Vec3 Cross(const Vec3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
		double Length() const;
		Vec2 XY() const { return { x, y }; }
	};

	enum class Surface : std::uint8_t
	{
		kGreen,  // normal carpet
		kWater,  // a hazard: one stroke penalty, back to where the shot was played from
		kFast,   // metal, ice: little rolling resistance
		kSlow    // sand, gravel: a lot
	};

	struct Tri
	{
		Vec3    a, b, c;
		Surface surface{ Surface::kGreen };
		// worked out by Hole::Finish
		Vec3   n;  // unit normal, pointing up
		double minx{ 0 }, miny{ 0 }, maxx{ 0 }, maxy{ 0 };
	};

	struct Wall
	{
		Vec2   a, b;
		double zmin{ 0.0 };
		double zmax{ 0.0 };
		double restitution{ 0.6 };
	};

	struct Obstacle
	{
		enum class Motion : std::uint8_t
		{
			kSpin,
			kSlide,
			kSwing,
			kWheel,
			kPendulum,
			kLift
		};

		std::string       name;   // the Blender object's name: the game moves the reference of the same name
		Motion            motion{ Motion::kSpin };
		Vec2              pivot;  // spin, swing, wheel, pendulum: the centre
		double            pivotZ{ 0.0 };
		Vec2              slide;  // slide: the furthest offset either way
		double            swingDegrees{ 0.0 };  // swing, pendulum: the furthest either way
		double            axisDegrees{ 0.0 };   // wheel, pendulum: the level axis' direction
		double            length{ 0.0 };        // pendulum: pivot to the ball's centre
		int               blades{ 4 };          // wheel
		double            halfDegrees{ 20.0 };  // wheel: how far either side of straight down a blade blocks
		double            height{ 0.0 };        // lift: how far it rises
		double            period{ 4.0 };
		double            phase{ 0.0 };
		std::vector<Wall> walls;  // in course space at time 0
		Vec3              origin;            // the mesh's origin at time 0, course space
		double            yawDegrees{ 0.0 };  // the mesh's turn about z at time 0

		// Where the obstacle is at time t, for the game to show it:
		//   spin, swing: a_angle = its turn about the vertical through pivot (radians, anticlockwise)
		//   wheel, pendulum: a_angle = its turn about the level axis (right hand rule along the axis)
		//   slide: a_offset; lift: a_lift (how high it's risen)
		void Pose(double a_time, double& a_angle, Vec2& a_offset, double& a_lift) const;
		// A point of the obstacle's walls at time 0, moved to time t.
		Vec2 Move(Vec2 a_point, double a_time) const;
		// How fast that point is moving.
		Vec2 Velocity(Vec2 a_pointNow, double a_time) const;
		// Whether the walls are there at all at time t (wheel, lift).
		bool Solid(double a_time, double a_ballRadius) const;
	};

	struct Hole
	{
		int                   number{ 0 };
		int                   par{ 2 };
		std::string           name;
		Vec3                  tee;
		double                teeYaw{ 0.0 };  // degrees, 0 = +y, clockwise seen from above (the game's convention)
		Vec3                  cup;
		double                cupRadius{ 3.78 };
		double                minx{ -1e9 }, miny{ -1e9 }, maxx{ 1e9 }, maxy{ 1e9 }, minz{ -1e9 };
		std::vector<Tri>      tris;
		std::vector<Wall>     walls;
		std::vector<Obstacle> obstacles;

		// Normals, bounding boxes and the lookup grid. Called by Course::Parse.
		void Finish();

		// The triangles that might be under (x, y) (indices into tris), from a grid built by Finish.
		const std::vector<int>& TrisNear(Vec2 a_xy) const;

	private:
		static constexpr double kCell = 16.0;
		double                        _gridX{ 0.0 }, _gridY{ 0.0 };
		int                           _nx{ 0 }, _ny{ 0 };
		std::vector<std::vector<int>> _cells;
		std::vector<int>              _none;
	};

	struct Course
	{
		std::vector<Hole> holes;

		// false and a_error ("line 12: ...") on a bad file.
		static bool Parse(const std::string& a_text, Course& a_out, std::string& a_error);
	};
}
