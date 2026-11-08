# The `.spr` format — shapes, depth, animations, creature profile

A text format that can be drawn by hand in any editor. At build time all
`assets/**/*.spr` files are packed into the binary (`tools/embed.c`). During development they
can be loaded straight from the directory and reloaded with the `r` key in the gallery:

```sh
./nightfall --gallery --assets assets   # gallery, 'r' = reload after editing a file
```

Each raster pixel is half a terminal cell (the `▀` character), so a pixel is
roughly square. All shapes are drawn **facing right**; mirroring is done
by the engine.

## Skeleton

```text
# comment (only at the start of a line)
sprite rabbit
tags creature ground small mammal shy
anchor 10 19              # default anchor (pivot) of the frames: x y in pixels

palette                   # local palette of the shape
  b 7c6853 ~4             # key, colour (hex), ~N = slight brightness noise (fur, bark)
  B 7c6853 ~4 z+1         # z+N / z-N = DEPTH offset for pixels with this key
  g 4e4136 ~3 z-2         # e.g. far limbs are behind the body
  w d8e0e8 glow           # glow = emits light (not affected by night fog): eyes, fireflies
  h 6ad0b0 a=90 glow      # a=N = translucency (glow, spider web), does not write z
end

frame sit                 # a new frame
|....gg..
|...gbBb.
|..bbbbbB
...

frame sit_blink copy sit  # copy of a frame + patch (space = unchanged)
patch 16 8
|d

frame hop1 at 11 19       # own anchor for this frame

depth                     # optional depth map for the last frame
|..11....                 # 1–9 = closer (+), a–i = farther (-1 … -9), '.' = from the palette

clip idle loop fps=6 : sit*24 sit_blink sit*16      # name*N = hold for N ticks
clip hop loop fps=10 speed=24 : hop1 hop2 hop3 hop4  # speed = movement in px/s
clip blink once fps=8 : sit sit_blink sit
clip sway param : bend0 bend1 bend2 bend3 bend4      # param = selected by a value -1…1 (wind)

prop something value      # an arbitrary property

creature                  # CREATURE PROFILE for the intent system
  move ground climb       # ground climb air perch hidden
  lane 0.2                # preferred ground row: 0 = standard lane by the trunks, 1 = front row near the camera
  speed walk=16 run=46 climb=30
  anim idle=idle move=hop run=run look=look eat=eat ...
  intents forage=5 wander=3 look=3 hide=2 leave=1
  homes bush log hollow   # where it hides / lives
  fear 0.6                # sensitivity to danger
  curiosity 0.5
  scares rabbit mouse     # (predator) whom it scares
  attracted glow          # (moth) flies towards light
  point head 7 -10        # head point relative to the anchor (peeking out of openings)
end
end
```

Named palettes shared between files: a `palette*.spr` file with a
`palette NAME … end` block outside any `sprite`; a shape then says `uses NAME`.

## Depth (z)

The final depth of a pixel = `z of the object in the scene` + `pixel offset` (from the palette or
from the `depth` map). The compositor draws with a z-test, so:

- far legs and the far ear (`z-2`) are behind the body and behind the grass blades in front of the creature,
- near legs (`z+1`) cover the body,
- a creature inside a bush (bush z -6) is visible only through an opening (-8), the leaves (-3…+3)
  hide it.

In the gallery: `d` = depth view (warm = closer), `o` = grass blades in front of and behind the shape.

## Ground rows (`lane`)

Ground creatures walk in rows of different depth. A closer row (higher z) is also lower on the
screen, so a creature in the front row walks among the foreground plants. `lane` is the preferred
row (default 0.15); the brain picks a row around it when wandering or foraging and now and then
one far from it. Climbing is only possible from the standard lane: a climber in a front row first
walks back to the trunk's depth. Large animals use a negative value (a bit further away).

## Logical animations (the `anim` mapping)

The intent system asks for logical animations; the profile maps them to clips of the shape.
Missing ones are replaced sensibly (e.g. `run` -> `move` -> `idle`).
A logical animation may list **variants** separated by `|` (`groom=play|scratch|shake`,
`rest=lie|sleep|sit`); each time the creature switches to it, one variant is picked at random
(repeat a name to make it more likely, max. 6 entries).

**Transitions:** if a clip `X_in` exists it is played once before `X` (lying down), and `X_out`
once when the creature leaves `X` (getting up). The creature does not move while they play.

| Logical | When |
|---|---|
| `idle` | standing / waiting (may include blinking, ear twitches) |
| `move`, `run` | movement on the ground / through the air (a clip with `speed`) |
| `look`, `alert`, `sniff` | looking around, alertness, sniffing |
| `eat`, `groom`, `rest`, `freeze` | eating, grooming, resting, freezing from fear |
| `climb`, `climb_down`, `cling` | climbing a trunk, a short stop on the bark |
| `play` | playful moves (play bow, pounce) after a chase or when meeting a packmate |
| `appear`, `vanish` | appearing / vanishing (eyes in the dark) |
| `hang`, `land`, `takeoff` | hanging head down, flipping over when landing, taking off (bat) |
| `hollow`, `hollow_peek` | only the eyes in a hollow / head poking out (owl) |

## Intents (`intents`)

`wander forage look groom rest hide peek leave climb perch flee follow watch pass hover sniff emerge vanish roost chase frolic`

`roost` = settle down: a creature with a `hollow` animation flies into a free visible hollow, a creature with `hang` hangs under a branch (it reserves the spot so that two do not hang in the same place).

`chase` = curiosity, not hunting: a sprint after any visible creature (even across the screen) from the profile list
`chases rabbit mouse …`, then `play` and `sniff`. `follow` on the ground = catch up with the
nearest member of the same species; a `pack N` animal far from the others gets a strong urge to
follow them, and alone it calls (`look`). `frolic` = happy among other animals (2+ creatures within ~90 px): runs into the middle of them, plays and sniffs; the more of them, the more likely. `rest_time MIN MAX` = how long a rest lasts (s).

The weight in the profile is the base; the brain multiplies it by drives (hunger -> `forage`,
fatigue -> `rest`, boredom -> `leave`, fear -> `hide`) and by randomness. Unreachable
intents (no trunk, no free hideout) get weight 0.

## Paths for creatures (`path.climbN`, `path.walkN`)

Environment models carry paths along which creatures move off the ground. The value is a
polyline `x y x y …` in pixels relative to the anchor (max. 40 points):

```text
prop path.climb0 0 0  1 -6  2 -12  1 -18 …     # climbing lane along a trunk, BOTTOM TO TOP
prop path.walk0  2 -7  8 -8  14 -8  20 -9 …    # walkway along the top of a branch / log / boulder
```

- A climbing path that starts at the ground is the entrance onto a tree; if it ends above the screen,
  a creature can leave along it.
- Paths whose end is near another path (≤ 12 px between a climbing path and a walkway) are
  **linked** automatically by the world: a squirrel climbs a trunk, walks onto a branch, eats, comes back.
- Hollows (`point.hollow`) are assigned to the nearest climbing path, so a creature
  climbs the trunk to get into them.
- Generators make these themselves: a trunk gets 2–3 lanes following its waviness, a branch a walkway
  along the top, a fallen log / boulder / stump a walkway along the top.
- `path.hangN` — a line under a branch where bats can hang head down (the anchor of their
  "hanging" frames is at the little claws on top). A branch exports it by itself.
- In the playground `d` shows the paths (pink = climbing, blue = walkway, orange = hanging,
  yellow = link).

## Hollow (`point.hollow`, `point.hollow_eye`, `hollow_size`)

- `point.hollow` — where a creature stands that climbs a trunk to get into the hollow (squirrel, dormouse).
- `point.hollow_eye` — the centre of the opening; the **eyes** of the creature sitting inside (owl) sit here.
  The "in the hollow" frames (`in_hollow`, `hollow_peek`) have the anchor exactly between the eyes
  (`frame hol_eyes at 9 7`), so only the eyes glow in the dark and when it pokes out the head sticks out of the opening.
- `hollow_size` — width and height of the opening in pixels.
- A creature inside a hollow (peeking, climbing in or out, the owl sitting in it) is **masked by the
  opening**: only its pixels inside the opening's ellipse are drawn (when climbing in / out also the
  part still below the opening, on the bark).

## Biomes (`assets/biomes/*.spr`)

A `biome NAME … end` block (outside any `sprite`) holds recommendation lists for the scene
composer — whitelists with weights, `name=weight` (no `=` means 1):

```text
biome taiga
  label       spruce taiga
  trees       trunk_spruce=5 trunk_pine=2 trunk_dead=1
  plants      fern=3 horsetail=2 grass_low=2
  ground      needles=6 moss=4 soil=1 stone=1
  creatures   squirrel=1.5 mouse owl=1.5 fox …
end
```

Keys: `label weight trees stumps logs bushes floor fungi plants plants_near plants_far
plant_density rocks moss puddles ground ground_scale creatures` (described in the header of
`assets/biomes/biomes.spr`). A missing list = everything allowed. `ground` types: `soil litter
needles moss grass stone gravel pebbles mud sand` — the floor strip is painted with noise patches
of these. `--biome auto` (default) picks one by the seed and `weight`, `--biome NAME` forces one,
`--biome off` ignores biomes (everything allowed, a random floor mix), `--biome list` lists them.
In the playground `B` cycles auto → each biome → off.

## Procedural shapes

Plants and environment (`grass`, `fern`, `bush`, `trunk`, `roots`, `branch`,
`vine`, `mushrooms`, `stone`, `log`, `litter`, `web`, `firefly` …) are generators
in `src/gen.c`. Each instance in the scene has its own seed, so every bush is different.
They export the same things as `.spr`: frames with depth, clips (`sway`, `rustle_l/r`,
`open0`, `close0`, `rustle0` …) and interaction points (`point.peek0`, `point.entry`,
`point.hollow`, `climb` …).

## Sketching poses

`tools/sketch_rig.py` (optional, for authoring only) composes poses from ellipses and capsules
with automatic shading and depth through palette keys, and writes `.spr`.
**Warning:** `--all` overwrites the files in `assets/creatures/`. Generate hand-edited shapes
into another file and carry the changes over manually.
