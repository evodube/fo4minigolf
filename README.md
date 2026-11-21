# fo4minigolf

An 18-hole mini golf course for Fallout 4, built as one workshop object for a settlement, played with the plugin's
own ball physics (like Playable Pool). Between holes you're moved to the next tee, so there's no walking.

## Playing

Build the **Mini Golf Course** from the workshop (Furniture > Mini Golf) and activate it (or press **Numpad 7**
anywhere on or near it) to start a round at hole 1.

**Hold** the activate key on the course instead to play the nearest settler 1v1 (or a companion: any grown-up,
friendly person within about 28 m). You putt first on every hole, then they play it; the lowest total over the 18
wins. They're invisible while you play (`[Opponent] bHide=0` has them stand by their ball instead); how true their
putts are is `[Opponent] fSkill` in the ini.

- **Aim:** look where you want the ball to go; the green line shows the line you're starting it on.
- **Putt:** hold the left mouse button, pull the mouse back for power (the meter at the bottom), let go.
- **Right mouse button / Q:** look down on the whole hole.
- **X:** end the round.

After each hole you're moved to the next tee, and the scorecard shows. A hole's over at 6 strokes. Water and out of
bounds cost a stroke and put the ball back where you played from. The course's windmill, rotor, doors and the rest
keep moving whether anyone's playing or not. Settings are in `Data\F4SE\Plugins\fo4minigolf.ini`.

Needs F4SE, the Address Library, and F4SE Menu Framework for the HUD. Works on 1.10.163 (OG), NG and AE.

## The course

About 18 m square, after a 1950s roadside putt-putt that came through the bombs: brick, stone, scrap metal and
Vault-Tec steel rails with moulded caps, real cut cups, mini golf carpet (the turf is only ever turf: the hazards are
sand bunkers, no water), raised tees, ramps, banked turns and drops, and a set piece on most holes: a Dutch smock
windmill, a streamline diner, a stone gatehouse, a crane and wrecking ball, Mr. Handy, three giant Nuka-Cola bottles. The
holes are laid out with a clear gap between every two of them (nothing touches), and the lawn between is planted with
the game's own clipped boxwoods, holly, roses and rocks (`tools/course/dressing.py`). The set pieces and holes use the
game's textures where it has a fitting one, and our own for the carpet, signs, rubber, steel, glass, canvas and lamps
(`tools/course/make_textures.py`).

| # | Hole | Par | Shape | What's in it |
|---|------|-----|-------|--------------|
| 1 | Welcome Mat | 2 | dogleg left | a mound, tyre stacks |
| 2 | Old Windmill | 3 | rise, then down and right | the windmill on a rise, its sails block the tunnel; a ramp down to the green, a bunker |
| 3 | Nuka Alley | 3 | banked S-bend, climbing | three giant Nuka-Cola bottles |
| 4 | Rust Ramp | 3 | steep ramp, hard left | a scrap-metal plateau, a bunker |
| 5 | Toxic Crossing | 2 | dumbbell | a hump between sand bunkers |
| 6 | Vertibird | 3 | uphill, right bend, big round green | a rotor sweeping the green, a bunker |
| 7 | Diner Dogleg | 2 | uphill into a banked 90 | the diner on the corner |
| 8 | Barrel Bumpers | 2 | bend into a lollipop | down onto a tilted round table of rusty oil drums that kick the ball back |
| 9 | Vault Doors | 3 | long straight, step down, right | two sliding blast doors in Vault-Tec frames |
| 10 | Tire Yard | 3 | ramp up to a clover | bouncy tyres |
| 11 | The Drop | 2 | steep covered chute, bend at the bottom | |
| 12 | Wrecking Ball | 2 | banked hairpin | a crane and swinging wrecking ball |
| 13 | The Fork | 3 | a straight that splits round a planter | a hump on the left route, a bunker on the right |
| 14 | Glowing Sea | 3 | wide wave | rolling ground, three sand bunkers |
| 15 | Castle Gate | 2 | ramp up, through the gate, bend | the gatehouse and a rising portcullis |
| 16 | Mr. Handy | 4 | curving barbell | his three arms sweeping the green, a bunker by the cup |
| 17 | Zigzag | 3 | three switchbacks down a hillside | terraces with a ramp down round each bend |
| 18 | Last Stop | 2 | banked sweep up into a funnel | a bunker at its mouth |

## Physics

`src/sim` is plain C++ with no game code, tested on Linux (`tests/sim_test.cpp`, `tests/course_play.cpp`):

- **Ground:** the holes' triangles, any number of levels. The ball rolls along the slope (5/7 g sin a for a rolling
  ball), flies off edges and crests, bounces when it lands. Carpet, fast, slow, sand (heavy drag, and a ball landing
  in a bunker stays there) and water surfaces.
- **Walls:** vertical segments with a height range. Moving obstacles turn, swing, slide, spin on a level axis
  (windmill sails, which block only while a sail is down), swing like a pendulum, or rise and drop (portcullis).
- **The cup:** a hole in the ground with a round rim the ball can catch: slow balls drop, fast ones skip over or
  lip out.
- **Hazards:** water and out of bounds: one stroke, back to where the shot was played from.
- **A bot** (`src/sim/Bot`) plays shots on a copy of the physics and keeps the one ending nearest the cup, measured
  round the walls. The course test has it play all 18 holes, and it's the settler's brain in a 1v1: worked out on worker
  threads a moment ahead of the putt (timing the obstacles), then put a little off line and weight by their skill.

## The plugin

`src/`: `Golf.cpp` is the game (the courses' obstacles turned every frame, your round, the shot camera, the ball,
putter and guide), `Hud.cpp` the HUD, and the rest is shared with Playable Pool, Wasteland Wagers and Amusement Games
(the Present hook, the free camera hooks, the menus watch, the sounds through XAudio2). `tools/make_esl.py` writes
`fo4minigolf.esl`; `tools/make_sounds.py` the sounds.

## Making the course

Blender 5.2 with PyNifly; see `tools/course/`:

1. `ba2.py` pulls the game's textures out of its archives so Blender can show them (nothing extracted ships).
2. `build_course.py` builds all 18 holes from code: each hole is a centreline (straights and arcs) plus round
   greens, merged into one outline, with a height along it (ramps, drops, banked bends); rails, bunkers, obstacles,
   cups and signs follow from it. `props.py` models the set pieces (lathe, sweep, bevelled boxes, lettering), and
   `dressing.py` plants the lawn with the game's own props.
3. `anneal.py` (with `layout.py`) finds where each hole goes: nothing overlapping (a 36-unit gap between any two
   holes, set pieces included).
4. `export_course.py` writes `course.txt` (the physics), `Course.nif` (one shape per texture, Havok collision to walk
   on, and each moving obstacle as a named node the plugin turns), and the ball, putter and guide dot.

`tools/course/blend/minigolf.blend` is the built scene.
