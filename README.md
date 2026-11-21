<div align="center">

<img src="docs/images/icon-256.png" width="128" alt="NIGHTFALL icon: an owl in a moonlit hollow">

# NIGHTFALL

**A living night forest in your terminal.**

Procedural pixel-art woodland drawn with Unicode half blocks and true colour.<br>
Creatures with their own intentions wander in, climb, hide, play, chase and fall asleep.<br>
Every scene is different. A frame takes about half a millisecond.

![NIGHTFALL — screensaver](docs/images/hero.gif)

[![CI](https://github.com/mrkuko/nightfall/actions/workflows/ci.yml/badge.svg)](https://github.com/mrkuko/nightfall/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mrkuko/nightfall?color=4c8)](https://github.com/mrkuko/nightfall/releases)
![C99](https://img.shields.io/badge/C99-no%20dependencies-blue)
![Platforms](https://img.shields.io/badge/Linux%20%7C%20macOS%20%7C%20BSD%20%7C%20Windows-terminal-555)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

</div>

---

## Features

- **Procedural scenes.** A seed picks a biome and a composition (a giant tree on the side, a grove,
  a clearing, a thicket …). Then it grows dozens of unique trees, stumps, logs, bushes, ferns, mushrooms
  and stones, scattered in depth. No two scenes are the same.
- **Real depth.** Every pixel carries its own depth. A per-pixel z-buffer compositor puts the fox's far
  legs behind its body and the grass in front of it. It hides a rabbit inside a bush except where
  the leaves part, and shows a squirrel in a hollow only through the opening.
- **Creatures with intentions.** 25 creatures: rabbits, squirrels, foxes, owls, bats, deer, boars,
  badgers, dholes, moles, fireflies and more. Each has drives (hunger, fatigue, boredom, fear) and picks
  intents: forage, climb a tree along a real path, roost in a hollow, hang under a branch, hide in a bush,
  peek out, chase out of curiosity, play with the pack, sleep. An owl may spot a mouse from its hollow, glide
  over, stoop and either catch it and fly off with it in its talons, or miss.
- **Living weather.** Gusts of wind roll through the grass from one side, with calm in between, and each
  plant is a damped spring that sways as a gust passes. An animal walking through bends only the plants it
  actually brushes against. Moonbeams, drifting mist and glowing fungi. Fog and rain come and go: a fog
  bank pours in from the side along the forest floor, a rain front sweeps across, one can roll out while
  the other moves in, and the swamp is foggier than the rocky pine wood. When it rains, dholes, foxes,
  rabbits and others lie down under the huge funnel-shaped leaves of butterbur; in fog they notice a
  predator later, and the owl hunts by ear.
- **Biomes.** Oak wood, beech hall, spruce taiga, rocky pine wood, birch grove, alder swamp, mixed forest.
  Each has its own trees, undergrowth, animals and ground (needles, moss, mud, gravel …).
- **Tiny and fast.** One ~500 kB binary with all art compiled in. Plain C99 with libc and libm only
  (no ncurses). About 0.4 ms per frame and a diff renderer with one `write()` per frame.
- **Moddable.** Shapes and animations live in a plain-text `.spr` format you can draw in any editor.
  Load them from a folder and hot-reload them in the gallery.

## Screenshots

| | |
|:---:|:---:|
| ![Oak wood](docs/images/biome-oak.png)<br>**Oak wood** | ![Spruce taiga](docs/images/biome-taiga.png)<br>**Spruce taiga** |
| ![Birch grove](docs/images/biome-birch.png)<br>**Birch grove** | ![Alder swamp](docs/images/biome-swamp.png)<br>**Alder swamp** |
| ![Rocky pine wood](docs/images/biome-pine.png)<br>**Rocky pine wood** | ![Beech hall](docs/images/biome-beech.png)<br>**Beech hall** |

| | | |
|:---:|:---:|:---:|
| ![Dhole pack](docs/images/dhole.gif)<br>A dhole pack plays and chases | ![Owl](docs/images/owl.gif)<br>An owl roosts in a hollow | ![Squirrel](docs/images/squirrel.gif)<br>A squirrel climbs, peeks out |
| ![Mole](docs/images/mole.gif)<br>A mole leaves molehills | ![Bat](docs/images/bat.gif)<br>A bat hangs under a branch | ![Deer](docs/images/deer.gif)<br>A deer grazes in the wind |
| ![Owl hunting a mouse](docs/images/owl-hunt.gif)<br>An owl catches a mouse and flies off with it | | |

## Weather

Fog and rain are two independent layers, each with a strength and a **front** that travels with the wind. A fog bank
pours in from the side along the forest floor (the lower part leads) and rolls out the same way. A rain front sweeps
across with slanted streaks, splashes and rings in the puddles. Because the layers are separate, fog can leave while
rain is arriving. A weather lasts a few minutes by default (`--weather-time`), and the biome tilts the odds: the swamp
is foggy, the rocky pine wood rarely.

![The six weather patterns: clear, mist, fog, drizzle, rain, rain and fog](docs/images/weather-patterns.png)

Animals react to it instead of just being drawn under it:

- **Rain.** Dholes, foxes, wildcats, badgers, weasels, rabbits and hedgehogs run to the nearest free spot under the big
  leaves of butterbur or hosta, lie there until it has been dry for a few seconds, shake themselves off and carry on.
  Drops land on the leaves (a splash, the leaf twitches), it stays dry underneath, and the hosta tips keep dripping
  for a while after the rain stops.
- **Fog.** Predators are noticed later, chases only start nearby, and the owl hunts by ear: it goes only for prey that
  moves, sniffs or eats.

![The rain front sweeps in and a dhole pack runs under the hosta leaves](docs/images/weather-rain.gif)
Rain comes in from the side: a drop here and there, then thicker. The dhole pack runs under the hosta leaves and lies down there.

![Fog rolling in from the left, a timelapse](docs/images/weather-fog.gif)
Fog pours in along the forest floor, at its real pace (a front needs several minutes to cross the screen).

Try it: `./nightfall --play --biome swamp --weather rainfog`, `--weather-in fog` starts clear and lets the fog roll in. Press `W` in the playground to roll in the next weather, `1`–`6` to choose one (it stays), `0` for auto again.

## Install

### Downloads

Everything is on the [releases page](https://github.com/mrkuko/nightfall/releases) and installs
offline. The Linux builds are static, so they have no dependencies.

| System | File | Install |
|---|---|---|
| Debian, Ubuntu | `nightfall_*_amd64.deb` / `arm64.deb` | `sudo apt install ./nightfall_*.deb` |
| Fedora | `nightfall-*.x86_64.rpm` / `aarch64.rpm` | `sudo dnf install ./nightfall-*.rpm` |
| Arch Linux | `nightfall-*.pkg.tar.zst` | `sudo pacman -U nightfall-*.pkg.tar.zst` |
| any Linux | `nightfall-*-linux-*.tar.gz` | unpack and run `nightfall` |
| macOS | `nightfall-*-macos-universal.tar.gz` | unpack and run `nightfall` |
| Windows | `nightfall-*-windows-x86_64.zip` | unpack; run `nightfall.exe` in Windows Terminal, or install `nightfall.scr` as a [real screensaver](#as-a-real-screensaver) |

Versions: **`vX.Y.0` is a full release**; `vX.Y.Z` with `Z` above 0 is a **nightly build**, marked as a
pre-release on the releases page.

### From source

All you need is a C99 compiler and `make`.

```sh
git clone https://github.com/mrkuko/nightfall
cd nightfall
make
./nightfall
sudo make install                   # optional: /usr/local/bin + man page (PREFIX=/usr, DESTDIR=… for packages)
```

**Windows:** build with [MSYS2](https://www.msys2.org/) (MinGW-w64):
`pacman -S make mingw-w64-x86_64-gcc`, then `make` (it builds `nightfall.exe` and the screensaver
`nightfall.scr`). Run it in **Windows Terminal**. The classic console host needs Windows 10 1809 or newer.

### Terminal requirements

- **True colour (24-bit)** and **Unicode**: kitty, Alacritty, WezTerm, foot, Ghostty,
  GNOME Terminal, Konsole, iTerm2, Windows Terminal …
- A font with the `▀` (upper half block) glyph. Practically every monospace font has one.
- A larger window means a bigger forest. The scene adapts to the terminal size and to resizing. In a
  very tall window (a 4K monitor with small text) the trees, the background trunks, the canopy and the
  moonbeams grow taller so that they reach beyond the top edge, and the trunks get more branches.

## Usage

```sh
nightfall                    # screensaver: watch, any key quits
nightfall --play             # playground: HUD, spawn animals, debug view
nightfall --gallery          # browse every shape and animation in the library
```

![Playground debug view: climbing lanes, walkways and hideouts](docs/images/playground.png)

<sub>The debug view (`d`): pink = climbing lanes, blue = walkways, orange = hanging lines, yellow = links,
crosses = hideouts and perches. The playground also shows a HUD with every creature's current intent.</sub>

### Options

| Option | |
|---|---|
| `--seed N` | reproducible scene and behaviour |
| `--scene-time S` | compose a new scene every *S* seconds (`0` = never, default `600`) |
| `--biome auto\|off\|NAME\|list` | pick a biome (`auto`: by the seed, `off`: anything goes) |
| `--weather auto\|NAME` | `auto` (default): fog and rain come and go; or one that stays: `clear mist fog drizzle rain rainfog` |
| `--weather-time M` | about how long one weather lasts, in minutes (default `6`, the real length is random, 0.6–1.4 ×; `0` = no weather) |
| `--event NAME` | start an event right away (also in `--play` and the screensaver): `fireflies`, `dholes` |
| `--spawn-freq F` | how often creatures arrive (`1` default, `2` twice as often, `0` none) |
| `--max-creatures N` | most creatures about at once (`0` = automatic, grows with the screen width) |
| `--spawn-rate NAME=F …` | override spawn chances, e.g. `--spawn-rate dhole=0.3 rabbit=0` |
| `--grass-density F` | foreground plant density (default `1`) |
| `--grass-spread F` | randomness of the gaps between plants (`0` even … `2` clumpy) |
| `--grass-zspread F` | depth of the foreground (`0` one layer, `1` default strip by the ground, `2` half way down the dirt, `3`+ all the way to the bottom edge; the creatures walk on that ground too) |
| `--assets DIR` | load `.spr` files from a folder instead of the built-in ones |
| `--help`, `--version` | |

```sh
nightfall --biome taiga --spawn-rate dhole=0.4 owl=0.3
nightfall --seed 42 --scene-time 0 --grass-density 0.6
```

### Playground keys

| Key | | Key | |
|---|---|---|---|
| `q` | quit | `space` | pause |
| `l` | new scene | `+` / `-` | simulation speed |
| `B` | next biome | `n` | someone arrives now |
| `h` | toggle HUD | `d` | show places, paths, links |
| `W` | next weather (the fronts start moving) | `1`–`6` | pick a weather and keep it: clear, mist, fog, drizzle, rain, rainfog |
| `0` | weather back to auto | `f` | the fireflies event now |
| `P` | dhole pack chases a deer now | | |

Spawn a creature: `r` rabbit · `s` squirrel · `x` fox · `o` owl · `v` bat · `e` eyes ·
`g` spirit · `j` deer · `b` boar · `c` wildcat · `m` mole · `w` weasel · `p` dhole

### Gallery keys

![Gallery: a dhole's frames in colour and in the depth view](docs/images/gallery.png)

| Key | | Key | |
|---|---|---|---|
| `←` `→` | previous / next shape | `↑` `↓` | previous / next animation |
| `space` `.` `,` | pause, step, step back | `z` | zoom |
| `d` | depth view | `o` | grass in front of / behind |
| `f` `m` `b` | flip, walk, background | `p` | properties / profile |
| `s` | regenerate a procedural shape | `r` | reload assets (`--assets`) |

### As a real screensaver

```sh
# tmux: start the forest after 5 idle minutes
set -g lock-after-time 300
set -g lock-command "nightfall"
```

Any key returns you to your work. You can also bind it to a shell alias, a `swayidle`/`xautolock`
hook that opens a fullscreen terminal, or just leave it running on a second monitor.

#### Windows

`nightfall.scr` is a native Windows screensaver: it needs no terminal, no `nightfall.exe` and no runtime.
The forest is drawn fullscreen on **every monitor** as one scene (animals walk from screen to screen; each
monitor is anchored at the bottom of the scene, so the ground is at the bottom everywhere). Any key, mouse
click or mouse movement quits.

* **Install:** right-click `nightfall.scr` in Explorer, **Install** (it asks for admin rights, copies the
  file to `System32` and opens the screensaver dialog). **Test** runs it once without installing it.

  ![Right-click nightfall.scr: Test, Configure, Install](docs/images/win-install.png)
* **Setting it as your screensaver:**
  1. Open the *Screen Saver Settings* dialog: right-click `nightfall.scr`, **Install** opens it for you.
     Later you can reach it from *Settings, Personalization, Lock screen, Screen saver*, or by typing
     "screen saver" into the Start menu.
  2. Pick **nightfall** in the *Screen saver* drop-down.
  3. Set **Wait** to the number of idle minutes after which it should start.
  4. Optional: **Preview** shows it fullscreen (any key or mouse movement quits), **Settings…** opens the
     Nightfall options (below).
  5. Click **OK** (or **Apply**).

  <img src="docs/images/win-screensaver-dialog.png" width="330" alt="The Windows Screen Saver Settings dialog with nightfall selected">

  To switch it off, choose *(None)* in the same drop-down. To remove it, delete `nightfall.scr` from
  `C:\Windows\System32`. A screensaver only starts when the PC is idle: it does not run while a video
  plays or a presentation is on.
* **SmartScreen:** the file is not code-signed, so Windows may warn when you open a downloaded `.scr` or
  the ZIP (*Windows protected your PC*): click **More info**, then **Run anyway**. Or right-click the
  downloaded ZIP, **Properties**, tick **Unblock**, before unpacking it.
* **Settings:** *Settings…* in the screensaver dialog (or `nightfall.scr /c`): the pixel size (auto, or the
  art-pixel size of the main monitor, default 300 rows), the foreground plants (`--grass-density`,
  `--grass-spread`, `--grass-zspread`), the biome and the spawn rates (`--spawn-rate`), how often the scene is replaced by a new one (minutes, default 10, `0` = never
  change it) and about how long one weather lasts (minutes, default 6, random around it, `0` = no weather). They are stored in `HKCU\Software\Nightfall`.

  <img src="docs/images/win-nightfall-settings.png" width="320" alt="The Nightfall settings dialog">

* **Trying options:** a `.scr` is started through the shell association, which drops command-line
  arguments, so run the same program as an exe: `make` also leaves `build\nightfall-saver.exe`, and
  `build\nightfall-saver.exe /s --height 400 --grass-density 2 --spawn-rate rabbit=0` runs it with those
  options (they override the saved settings and are not saved). `--info` shows the monitors and the scene
  size instead of running.

## Biomes

| Biome | Trees | Ground | Typical visitors |
|---|---|---|---|
| Oak wood (`oak_wood`) | oak, old oak, ivy oak, beech | leaf litter, soil, moss | rabbit, squirrel, badger, fox, deer, stag beetle |
| Beech hall (`beech_hall`) | beech | deep leaf litter | squirrel, dormouse, owl, wildcat |
| Spruce taiga (`taiga`) | spruce, pine | needles, moss | squirrel, owl, dhole, boar |
| Rocky pine wood (`pine_rocks`) | pine, dead pine | stone, gravel, sand | fox, weasel, wildcat, dhole |
| Birch grove (`birch_grove`) | birch | grass, moss | rabbit, mole, hedgehog, deer, frog |
| Alder swamp (`swamp`) | birch, dead trees | mud, moss, puddles | frog, salamander, snail, bat, fireflies |
| Mixed forest (`mixed`) | a bit of everything | a bit of everything | anyone |

The name in brackets is what `--biome` takes (`--biome taiga`; `--biome list` prints them all, `--biome off`
turns the recommendations off, the default `auto` picks one at random). Biomes are plain data
(`assets/biomes/biomes.spr`): weighted lists of trees, plants, animals and
floor patches. Add your own without touching the code.

## Creatures

| | | |
|---|---|---|
| **Rabbit** forages, grooms, freezes, bolts into a bush | **Squirrel** climbs trunks along real paths, runs out onto branches, hides in hollows | **Fox** trots, sniffs and hunts rabbits, mice and moles with a crouch and a pounce |
| **Owl** roosts in a hollow with only its eyes glowing and pokes its head out | **Bat** hangs upside down under branches | **Dhole** lives in packs, plays, chases out of curiosity, sleeps curled up |
| **Deer** grazes with its head down and blinks | **Mole** surfaces here and there and leaves molehills | **Hedgehog, badger, boar, wildcat, weasel, mouse, dormouse** |
| **Frog, salamander, snail, slug** | **Beetle, stag beetle, moth, fireflies, spider** | **Eyes in the dark, forest spirit** |

![Creature sheet](docs/images/creatures-sheet.png)

## How it works

- **Half-block raster.** Each terminal cell shows two square pixels with `▀` (foreground = top,
  background = bottom). A 200×50 terminal gives a 200×100 pixel canvas.
- **Depth compositor.** Every sprite pixel has a depth offset. Objects are blitted with a z-test into one
  buffer, with distance fog by depth. Static scenery is baked once, so each frame only copies it and
  draws what moves.
- **Diff renderer.** Only changed cells are written, inside synchronized-output brackets, in one
  `write()` per frame on the alternate screen.
- **Intents.** A small brain per creature: drives × profile weights × randomness pick an intent. The
  intent becomes a plan of steps (walk, climb a path, enter a hideout, peek, play an animation). Fear,
  noises and other creatures interrupt it.
- **Navigation.** Trees, branches, logs and boulders export climbing lanes, walkways and hanging
  lines. The world links nearby paths, so creatures can go from the ground up a trunk and out onto
  a branch.
- **Wind.** A slowly turning direction and travelling gust waves. Each plant is a damped spring.

## Make your own creatures

Shapes are text. Draw frames with palette keys, give each key a colour and a depth, and define
clips and a behaviour profile:

```text
sprite beetlet
tags creature ground tiny insect
anchor 5 4
palette
  b 3a4a2c ~4          # colour with a little noise
  B 3a4a2c ~4 z+1      # same colour, one step closer (near legs)
  g 1e2616 z-1         # far legs, behind the body
  e 0a0806
end
frame step0
|..bbbbb...
|.bbbbbbbe.
|.bbbbbbbbb
|..B.g.B.g.
|..........
frame step1 copy step0
patch 0 3
|..g.B.g.B.
clip walk loop fps=8 speed=6 : step0 step1
clip idle loop fps=2 : step0
creature
  move ground
  speed walk=6 run=14
  anim idle=idle move=walk
  intents wander=4 forage=3 hide=1
  homes log
end
end
```

```sh
nightfall --gallery --assets assets   # edit a file, press r to reload
```

The full format, including depth maps, frame patches, clips, transitions, paths, hollows and biomes, is
documented in [`assets/FORMAT.md`](assets/FORMAT.md). Many creatures are posed with a tiny optional
Python rig ([`tools/sketch_rig.py`](tools/sketch_rig.py)) that builds them from ellipses and capsules.

## Project layout

```text
src/          engine: terminal, raster + z-buffer, sprites, generators, world, creatures, scenes
win/          Windows screensaver (nightfall.scr): a GDI window per monitor
assets/       creatures (.spr), biomes, FORMAT.md
tools/        embed.c (packs assets into the binary), sketch_rig.py (pose sketching)
docs/         man page, screenshots
packaging/    nfpm definition for the .deb / .rpm / Arch packages
```

## Contributing

Bug reports, new creatures, biomes and terminal compatibility notes are very welcome. Please keep it
C99 and dependency-free. `make CFLAGS="-std=c99 -Wall -Wextra -pedantic"` should stay warning-free.

### Regenerating the images

Every picture in this README is rendered by the program itself, headlessly (no terminal needed):

```sh
sh tools/make_screenshots.sh            # needs ImageMagick and ffmpeg
```

It uses `--snap`, `--record`, `--follow` and `--sheet`; see `nightfall --help`.

## License

MIT, see [LICENSE](LICENSE).

<div align="center"><sub>Inspired by <a href="https://robobunny.com/projects/asciiquarium/html/">asciiquarium</a>.</sub></div>
