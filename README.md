# fo4minigolf

An 18-hole mini golf course for Fallout 4, built as one workshop object for a settlement, played with the plugin's
own ball physics (like Playable Pool). Between holes you're moved to the next tee, so there's no walking.

**Status: the course and its physics.** The F4SE plugin (placing the course, the shot camera, the HUD and scorecard,
moving between holes) and the ESL come next.

## The course

About 16.5 m square: a dry-grass lot with paved edging round every hole, the holes all different shapes and wound
round each other, each tee a short walk from the last cup. Everything uses the game's own textures except the hole
signs and the flags (`tools/course/make_textures.py`).

| # | Hole | Par | Shape | What's in it |
|---|------|-----|-------|--------------|
| 1 | Welcome Mat | 2 | keyhole | a little hump |
| 2 | Old Windmill | 3 | keyhole | the sails block the tunnel (a level-axis wheel) |
| 3 | Nuka Alley | 2 | S-bend | giant bottles |
| 4 | Rust Ramp | 3 | L | a ramp up to the plateau |
| 5 | Toxic Crossing | 3 | dumbbell | a plank bridge over muck (water hazard) |
| 6 | Vertibird | 3 | lollipop | a rotor sweeping the round green |
| 7 | Diner Dogleg | 3 | dogleg | round the diner |
| 8 | The Volcano | 3 | lollipop | the cup on top of a cone |
| 9 | Vault Doors | 3 | long keyhole | two sliding blast doors |
| 10 | Tire Yard | 2 | clover | bouncy tyres |
| 11 | The Drop | 2 | L, downhill | a chute to the lower green |
| 12 | Wrecking Ball | 3 | hairpin | a swinging wrecking ball |
| 13 | Fat Man Jump | 3 | island | a ramp jump over muck to an island green |
| 14 | Glowing Sea | 3 | wave | rolling ground, slag (slow) and a toxic pool |
| 15 | Castle Gate | 3 | bend | a portcullis that rises and drops |
| 16 | Mr. Handy | 3 | barbell | three spinning arms |
| 17 | Zigzag | 3 | switchbacks | |
| 18 | Last Stop | 3 | sweep | uphill into a funnel |

## Physics

`src/sim` is plain C++ with no game code, tested on Linux (`tests/sim_test.cpp`, `tests/course_play.cpp`):

- **Ground:** the holes' triangles, any number of levels. The ball rolls along the slope (5/7 g sin a for a rolling
  ball), flies off edges and crests, bounces when it lands. Carpet, fast, slow and water surfaces.
- **Walls:** vertical segments with a height range. Moving obstacles turn, swing, slide, spin on a level axis
  (windmill sails, which block only while a sail is down), swing like a pendulum, or rise and drop (portcullis).
- **The cup:** a hole in the ground with a round rim the ball can catch: slow balls drop, fast ones skip over or
  lip out.
- **Hazards:** water and out of bounds: one stroke, back to where the shot was played from.
- **A bot** (`src/sim/Bot`) plays shots on a copy of the physics and keeps the one ending nearest the cup, measured
  round the walls. The course test has it play all 18 holes; it'll be the settlers' brain later.

## Making the course

Blender 5.2 with PyNifly; see `tools/course/`:

1. `ba2.py` pulls the game's textures out of its archives so Blender can show them (nothing extracted ships).
2. `build_course.py` builds all 18 holes from code: each hole is a centreline (straights and arcs) plus round
   greens, merged into one outline; rails, ramps, obstacles, cups, flags and signs follow from it.
3. `anneal.py` (with `layout.py`) finds where each hole goes: nothing overlapping, room for paths, short walks.
4. `export_course.py` writes `course.txt` (the physics), `Course.nif` (everything still, one shape per texture,
   with Havok collision to walk on), one nif per moving obstacle, and the ball and putter.

`tools/course/blend/minigolf.blend` is the built scene.
