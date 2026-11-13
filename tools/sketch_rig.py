#!/usr/bin/env python3
"""sketch_rig.py — an OPTIONAL helper for sketching creature poses into the .spr format.

The NIGHTFALL runtime is pure C and does not need this script. It only serves for quickly
drafting basic shapes (body, head, ears, paws as ellipses/capsules with automatic
shading from the upper right and depth through palette keys).
The result is saved as .spr and can be refined by hand afterwards.

Usage:
    python3 tools/sketch_rig.py rabbit > assets/creatures/rabbit.spr
    python3 tools/sketch_rig.py --all assets/creatures     # all creatures
    python3 tools/sketch_rig.py --list

WARNING: --all overwrites the .spr files. If you have edited them by hand, generate only
a single creature into another file and carry the changes over manually.

Diely:
    E(cx, cy, rx, ry, ang, keys, fuzz)     elipsa
    C(x0, y0, x1, y1, r0, r1, keys, fuzz)  capsule (limb, ear, tail)
    P(x, y, key)                           jeden pixel (oko, nos, odlesk)
    keys = 'lmd' -> palette keys for the (light, middle, dark) part of the piece
    fuzz = 0..1 -> randomly ragged edge (fur, tail, spines)
Depth is set by the palette: e.g. the keys of the far limbs have z-2.
"""
import math
import os
import sys


# --------------------------------------------------------------------------
# primitives
# --------------------------------------------------------------------------

def h2(x, y, s=0):
    v = (x * 374761393 + y * 668265263 + s * 2246822519) & 0xFFFFFFFF
    v = ((v ^ (v >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((v ^ (v >> 16)) & 0xFFFF) / 65535.0


class E:
    def __init__(s, cx, cy, rx, ry, ang=0.0, keys='bad', fuzz=0.0):
        s.cx, s.cy, s.rx, s.ry, s.ang, s.keys, s.fuzz = cx, cy, rx, ry, ang, keys, fuzz

    def inside(s, x, y):
        dx, dy = x - s.cx, y - s.cy
        c, n = math.cos(-s.ang), math.sin(-s.ang)
        u, v = dx * c - dy * n, dx * n + dy * c
        return (u / s.rx) ** 2 + (v / s.ry) ** 2 <= 1.0


class C:
    def __init__(s, x0, y0, x1, y1, r0, r1=None, keys='bad', fuzz=0.0):
        s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
        s.r0, s.r1 = r0, (r0 if r1 is None else r1)
        s.keys, s.fuzz = keys, fuzz

    def inside(s, x, y):
        vx, vy = s.x1 - s.x0, s.y1 - s.y0
        vv = vx * vx + vy * vy
        t = 0.0 if vv == 0 else max(0.0, min(1.0, ((x - s.x0) * vx + (y - s.y0) * vy) / vv))
        px, py = s.x0 + vx * t, s.y0 + vy * t
        r = s.r0 + (s.r1 - s.r0) * t
        return (x - px) ** 2 + (y - py) ** 2 <= r * r + 0.15


class P:
    def __init__(s, x, y, key):
        s.x, s.y, s.key = x, y, key


class Clear:
    """Clears the pixels in an ellipse (e.g. the gap between the legs)."""
    def __init__(s, cx, cy, rx, ry):
        s.e = E(cx, cy, rx, ry)


def chain(points, radii, keys, fuzz=0.0):
    """A smooth "tube" along points (tails): interpolated circles with a ~0.7 px step,
    drawn as a single piece so that the individual links are not visible."""
    circles = []
    for i in range(len(points) - 1):
        (x0, y0), (x1, y1) = points[i], points[i + 1]
        r0, r1 = radii[i], radii[i + 1]
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) / 0.7))
        for k in range(n):
            t = k / n
            circles.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r0 + (r1 - r0) * t))
    circles.append((points[-1][0], points[-1][1], radii[-1]))
    return [Union(circles, keys, fuzz)]


class Union:
    def __init__(s, circles, keys, fuzz):
        s.circles, s.keys, s.fuzz = circles, keys, fuzz

    def inside(s, x, y):
        return any((x - cx) ** 2 + (y - cy) ** 2 <= r * r for cx, cy, r in s.circles)


def render(parts, w, h, rot=0.0, pivot=(0, 0), seed=1):
    grid = [['.'] * w for _ in range(h)]
    cr, sr = math.cos(-rot), math.sin(-rot)
    px0, py0 = pivot

    def to_local(x, y):
        dx, dy = x - px0, y - py0
        return px0 + dx * cr - dy * sr, py0 + dx * sr + dy * cr

    for pi, part in enumerate(parts):
        if isinstance(part, P):
            dx, dy = part.x - px0, part.y - py0
            c2, s2 = math.cos(rot), math.sin(rot)
            x, y = round(px0 + dx * c2 - dy * s2), round(py0 + dx * s2 + dy * c2)
            if 0 <= x < w and 0 <= y < h:
                grid[y][x] = part.key
            continue
        if isinstance(part, Clear):
            for y in range(h):
                for x in range(w):
                    if part.e.inside(*to_local(x + 0.5, y + 0.5)):
                        grid[y][x] = '.'
            continue
        mask = [[part.inside(*to_local(x + 0.5, y + 0.5)) for x in range(w)] for y in range(h)]
        if part.fuzz > 0:
            edge = [[mask[y][x] and any(
                not (0 <= y + dy < h and 0 <= x + dx < w and mask[y + dy][x + dx])
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) for x in range(w)] for y in range(h)]
            for y in range(h):
                for x in range(w):
                    if edge[y][x] and h2(x, y, seed + pi) < part.fuzz * 0.6:
                        mask[y][x] = False
        lk, mk, dk = part.keys[0], part.keys[1], part.keys[2]
        for y in range(h):
            for x in range(w):
                if not mask[y][x]:
                    continue
                up = y > 0 and mask[y - 1][x]
                right = x < w - 1 and mask[y][x + 1]
                down = y < h - 1 and mask[y + 1][x]
                left = x > 0 and mask[y][x - 1]
                upright = y > 0 and x < w - 1 and mask[y - 1][x + 1]
                if not up or (not right and not upright):
                    k = lk
                elif not down or not left:
                    k = dk
                else:
                    k = mk
                grid[y][x] = k
    return grid


def emit_frame(out, name, parts, w, h, at=None, rot=0.0, pivot=None):
    grid = render(parts, w, h, rot, pivot or (w / 2, h / 2))
    out.append('frame %s%s' % (name, (' at %d %d' % at) if at else ''))
    for r in grid:
        out.append('|' + ''.join(r).rstrip('.').ljust(1, '.'))
    out.append('')


def legs_walk(phase, hip_x, hip_y, ground, length, swing=0.38, lift=1.2):
    """Foot position while walking: phase 0..2π, returns the foot's (x, y)."""
    a = math.sin(phase) * swing
    fx = hip_x + math.sin(a) * length
    fy = min(ground, hip_y + math.cos(a) * length)
    if math.cos(phase) > 0.2:          # the leg is swinging forward -> lifted
        fy -= lift * math.cos(phase)
    return fx, fy


# ==========================================================================
# RABBIT
# ==========================================================================

RABBIT_HEAD = '''# rabbit — generated by tools/sketch_rig.py, feel free to edit further
sprite rabbit
tags creature ground small mammal shy
anchor 10 19
palette
  b 7c6853 ~4
  a 5c4c3e ~4
  d 3b3028 ~3
  B 7c6853 ~4 z+1
  A 5c4c3e ~4 z+1
  D 3b3028 ~3 z+1
  g 4e4136 ~3 z-2
  f 3c322a ~3 z-2
  F 2a231d ~2 z-2
  c 9c8c76 ~3
  C 8a7a66 ~3
  t b8b0a2 ~4
  T 8c8478 ~3
  p 7a5552
  e 0a0807
  w d8e0e8 glow
  n 8a5c58
end
'''

RABBIT_TAIL = '''
creature
  move ground
  lane 0.45
  speed walk=16 run=46
  anim idle=idle move=hop run=run look=look alert=alert eat=eat groom=groom rest=rest freeze=freeze sniff=sniff
  intents forage=5 wander=3 look=3 groom=2 rest=1 hide=2 leave=1
  homes bush log hollow
  fear 0.6
  curiosity 0.5
  size small
  point head 7 -10
end
end
'''


def rabbit_parts(pose):
    p = dict(bx=9.6, by=14.0, brx=6.0, bry=5.0, bang=-0.15,
             hx=16.0, hy=9.0, hang=0.15, hr=(3.8, 3.1),
             ear=-0.35, ear2=-0.5, earlen=7.0,
             fl=(15.0, 14.0, 15.4, 18.6), fl2=(16.2, 14.0, 16.6, 18.6),
             foot=(8.6, 18.5, 4.4, 1.3), haunch=(7.2, 15.2, 4.8, 4.0),
             tail=(3.4, 13.8), eye=1, nose_dy=0.0, chest=True)
    p.update(pose)
    hx, hy = p['hx'], p['hy']
    parts = []
    a2 = p['ear2']
    ex, ey = hx - 1.0, hy - 2.0
    parts.append(C(ex, ey, ex + math.sin(a2) * p['earlen'], ey - math.cos(a2) * p['earlen'], 1.3, 0.9, 'gfF'))
    if p['fl2']:
        x0, y0, x1, y1 = p['fl2']
        parts.append(C(x0, y0, x1, y1, 1.0, 0.9, 'gfF'))
    if p['tail']:
        parts.append(E(p['tail'][0], p['tail'][1], 1.5, 1.4, 0, 'tTT', 0.4))
    parts.append(E(p['bx'], p['by'], p['brx'], p['bry'], p['bang'], 'bad', 0.15))
    hxx, hyy, hrx, hry = p['haunch']
    parts.append(E(hxx, hyy, hrx, hry, -0.2, 'bad', 0.1))
    if p['chest']:
        parts.append(E(hx - 2.0, hy + 4.4, 1.9, 2.8, 0.2, 'cCa', 0.2))
    parts.append(E(hx, hy, p['hr'][0], p['hr'][1], p['hang'], 'bad'))
    parts.append(E(hx + 2.5 * math.cos(p['hang']), hy + 1.0 + 2.5 * math.sin(p['hang']), 1.9, 1.6, p['hang'], 'bad'))
    fx, fy, frx, fry = p['foot']
    parts.append(E(fx, fy, frx, fry, 0, 'BAD'))
    if p['fl']:
        x0, y0, x1, y1 = p['fl']
        parts.append(C(x0, y0, x1, y1, 1.1, 1.0, 'BAD'))
    a = p['ear']
    ex, ey = hx - 0.2, hy - 2.2
    tx, ty = ex + math.sin(a) * p['earlen'], ey - math.cos(a) * p['earlen']
    parts.append(C(ex, ey, tx, ty, 1.5, 1.0, 'BAD'))
    for k in range(2, int(p['earlen']) - 1):
        t = k / p['earlen']
        parts.append(P(round(ex + (tx - ex) * t - 0.2), round(ey + (ty - ey) * t), 'p'))
    ca, sa = math.cos(p['hang']), math.sin(p['hang'])
    ox, oy = hx + 1.0 * ca + 0.6 * sa, hy - 0.6 + 1.0 * sa
    if p['eye'] == 1:
        parts += [P(round(ox), round(oy), 'e'), P(round(ox), round(oy) + 1, 'e'), P(round(ox) + 1, round(oy), 'w')]
    elif p['eye'] == 0:
        parts += [P(round(ox), round(oy) + 1, 'd'), P(round(ox) + 1, round(oy) + 1, 'd')]
    nx, ny = hx + 4.3 * ca, hy + 0.6 + 4.3 * sa + p['nose_dy']
    parts.append(P(round(nx), round(ny), 'n'))
    return parts


RABBIT_POSES = [
    ('sit', {}),
    ('sit_blink', dict(eye=0)),
    ('sit_ear', dict(ear=0.15, ear2=-0.7)),
    ('sit_ear2', dict(ear=-0.6, ear2=-0.2)),
    ('sniff', dict(nose_dy=-1, hang=0.05)),
    ('sniff2', dict(nose_dy=0, hang=0.22, hy=9.4)),
    ('freeze', dict(by=15.2, bry=4.0, brx=6.8, hy=12.0, hx=16.4, hang=0.25, ear=-1.25, ear2=-1.35,
                    earlen=6.0, haunch=(7.0, 16.0, 4.8, 3.3), fl=(15.0, 15.5, 16.2, 18.6), fl2=None)),
    ('alert', dict(bx=10.0, by=12.6, brx=4.6, bry=6.2, bang=-0.5, hx=14.4, hy=5.4, hang=0.05,
                   ear=-0.05, ear2=-0.2, earlen=7.2, haunch=(7.8, 15.2, 4.2, 3.9),
                   fl=(13.6, 11.0, 14.6, 13.8), fl2=(14.6, 11.0, 15.4, 13.6), tail=(4.4, 15.6))),
    ('look', dict(hx=13.4, hy=8.4, hang=math.pi - 0.15, ear=0.4, ear2=0.55)),
    ('eat', dict(hx=17.4, hy=15.0, hang=0.7, ear=-0.9, ear2=-1.1, earlen=6.5,
                 fl=(14.6, 14.6, 15.0, 18.6), chest=False)),
    ('eat2', dict(hx=17.4, hy=15.4, hang=0.75, ear=-0.95, ear2=-1.1, earlen=6.5, nose_dy=1,
                  fl=(14.6, 14.6, 15.0, 18.6), chest=False)),
    ('groom', dict(bx=9.8, by=13.4, bang=-0.35, hx=15.4, hy=8.0, hang=0.45, ear=-0.6, ear2=-0.8,
                   fl=(14.6, 12.8, 17.0, 10.4), fl2=(15.2, 12.6, 17.6, 10.8), eye=0)),
    ('groom2', dict(bx=9.8, by=13.4, bang=-0.35, hx=15.4, hy=8.4, hang=0.6, ear=-0.7, ear2=-0.9,
                    fl=(14.6, 12.8, 17.4, 11.4), fl2=(15.2, 12.6, 17.8, 11.6), eye=0)),
    ('rest', dict(by=15.6, bry=3.8, brx=7.0, hx=16.6, hy=12.4, hang=0.1, ear=-1.3, ear2=-1.4,
                  earlen=6.8, haunch=(7.0, 16.2, 4.6, 3.0), fl=None, fl2=None, foot=(10.0, 18.6, 5.0, 1.0))),
    ('rest_blink', dict(by=15.6, bry=3.8, brx=7.0, hx=16.6, hy=12.4, hang=0.1, ear=-1.3, ear2=-1.4,
                        earlen=6.8, haunch=(7.0, 16.2, 4.6, 3.0), fl=None, fl2=None,
                        foot=(10.0, 18.6, 5.0, 1.0), eye=0)),
    ('hop1', dict(by=15.0, bry=4.2, brx=6.6, bang=-0.05, hx=16.6, hy=11.0, hang=0.25, ear=-0.9, ear2=-1.05,
                  haunch=(7.0, 15.8, 4.6, 3.4), fl=(15.0, 15.0, 16.4, 18.6), fl2=None)),
    ('hop2', dict(bx=10.0, by=12.6, brx=7.0, bry=4.0, bang=0.25, hx=17.6, hy=9.2, hang=0.2, ear=-1.1,
                  ear2=-1.25, haunch=(7.0, 13.8, 4.2, 3.4), foot=(3.6, 17.6, 4.0, 1.1),
                  fl=(15.2, 14.0, 17.6, 17.2), fl2=None, tail=(3.0, 12.4))),
    ('hop3', dict(bx=11.0, by=11.4, brx=7.2, bry=3.8, bang=0.05, hx=18.6, hy=8.6, hang=0.1, ear=-1.2,
                  ear2=-1.35, haunch=(7.6, 12.0, 4.2, 3.3), foot=(4.8, 14.6, 4.0, 1.1),
                  fl=(16.0, 13.2, 19.2, 14.6), fl2=(16.8, 13.2, 19.6, 15.2), tail=(4.0, 10.4))),
    ('hop4', dict(bx=11.0, by=13.0, brx=6.8, bry=4.2, bang=-0.3, hx=17.6, hy=11.4, hang=0.35, ear=-1.0,
                  ear2=-1.15, haunch=(7.8, 12.8, 4.2, 3.4), foot=(7.8, 15.6, 4.0, 1.1),
                  fl=(16.0, 14.4, 17.4, 18.6), fl2=None, tail=(4.2, 11.0))),
]

RABBIT_CLIPS = '''
clip idle loop fps=6 : sit*24 sit_blink sit*16 sit_ear sit_ear sit_ear2 sit*12 sniff sit sniff sit*10 sit_blink sit*8
clip blink once fps=8 : sit sit_blink sit
clip ear_twitch once fps=8 : sit sit_ear sit_ear2 sit_ear sit
clip sniff loop fps=6 : sniff sniff2 sniff sit sniff sniff2 sit*4
clip alert loop fps=4 : alert*12 sit_blink alert*6
clip look loop fps=4 : look*10 sit look*6
clip eat loop fps=6 : eat eat2 eat eat2 eat eat*4 eat2
clip groom loop fps=7 : groom groom2 groom groom2 groom groom2 sit
clip rest loop fps=3 : rest*14 rest_blink*8 rest*6
clip freeze loop fps=2 : freeze
clip hop loop fps=10 speed=24 : hop1 hop2 hop3 hop4
clip run loop fps=14 speed=46 : hop2 hop3 hop4 hop1
'''


def rabbit(out):
    out.append(RABBIT_HEAD)
    for name, pose in RABBIT_POSES:
        emit_frame(out, name, rabbit_parts(pose), 22, 20)
    out.append(RABBIT_CLIPS)
    out.append(RABBIT_TAIL)


# ==========================================================================
# SQUIRREL
# ==========================================================================

SQUIRREL_HEAD = '''# squirrel — generated by tools/sketch_rig.py
sprite squirrel
tags creature ground climber small mammal
anchor 11 21
palette
  b 8e5b3a ~5
  a 6a4128 ~4
  d 432818 ~3
  B 8e5b3a ~5 z+1
  A 6a4128 ~4 z+1
  D 432818 ~3 z+1
  g 54331f ~3 z-2
  f 3e2616 ~3 z-2
  F 2a1a10 ~2 z-2
  u 9c6a46 ~8 z-1
  v 74482c ~7 z-1
  x 4c2e1c ~5 z-1
  c a89884 ~3
  C 8c7c68 ~3
  k 6a5638 z+2
  e 0a0807
  w d8e0e8 glow
  n 3a2a24
end
'''

SQUIRREL_TAIL = '''
creature
  move ground climb
  lane 0.2
  speed walk=20 run=55 climb=30
  anim idle=idle move=run run=run look=look eat=eat climb=climb climb_down=climb_down cling=cling alert=alert groom=flick
  intents forage=4 wander=3 climb=5 look=3 rest=1 hide=2 leave=2
  homes hollow branch
  fear 0.5
  curiosity 0.7
  size small
  point head 4 -12
end
end
'''


def squirrel_parts(pose):
    p = dict(bx=11.6, by=15.0, bang=-0.25, brx=3.6, bry=5.0,
             hx=15.0, hy=8.8, hang=0.2,
             tail=[(6.8, 17.0), (4.6, 13.4), (4.2, 9.0), (5.8, 5.4), (8.4, 3.6)],
             trad=[2.8, 3.3, 3.2, 2.7, 2.0],
             paws=(14.6, 12.6, 16.0, 11.4), nut=True, eye=1, ear=0.0,
             foot=(12.0, 20.4, 3.4, 1.0), haunch=(10.2, 17.6, 3.4, 3.2), belly=True)
    p.update(pose)
    parts = []
    parts += chain(p['tail'], p['trad'], 'uvx', 0.55)
    hx, hy, ha = p['hx'], p['hy'], p['hang']
    ca, sa = math.cos(ha), math.sin(ha)
    # far ear
    parts.append(C(hx - 1.4, hy - 2.0, hx - 2.0 + p['ear'], hy - 4.4, 1.0, 0.5, 'gfF'))
    fx, fy, frx, fry = p['foot']
    parts.append(E(fx + 0.8, fy, frx, fry, 0, 'gfF'))
    parts.append(E(p['bx'], p['by'], p['brx'], p['bry'], p['bang'], 'bad', 0.2))
    hxx, hyy, hrx, hry = p['haunch']
    parts.append(E(hxx, hyy, hrx, hry, 0, 'bad', 0.15))
    if p['belly']:
        parts.append(E(p['bx'] + 1.8 * math.cos(p['bang'] + 1.57), p['by'] + 0.4, 1.4, p['bry'] * 0.7, p['bang'], 'cCa'))
    parts.append(E(hx, hy, 3.0, 2.6, ha, 'bad'))
    parts.append(E(hx + 2.2 * ca, hy + 0.9 + 2.2 * sa, 1.5, 1.3, ha, 'bad'))
    parts.append(E(fx, fy, frx, fry, 0, 'BAD'))
    if p['paws']:
        x0, y0, x1, y1 = p['paws']
        parts.append(C(x0, y0, x1, y1, 0.9, 0.8, 'BAD'))
        if p['nut']:
            parts += [P(round(x1) + 1, round(y1) - 1, 'k'), P(round(x1) + 1, round(y1), 'k')]
    # near ear with a tuft
    parts.append(C(hx - 0.6, hy - 2.0, hx - 1.0 + p['ear'], hy - 4.6, 1.0, 0.5, 'BAD'))
    parts.append(P(round(hx - 1.0 + p['ear']), round(hy - 5.4), 'D'))
    ox, oy = hx + 0.9 * ca + 0.5 * sa, hy - 0.5 + 0.9 * sa
    if p['eye']:
        parts += [P(round(ox), round(oy), 'e'), P(round(ox) + 1, round(oy), 'w')]
        if p.get('bigeye'):
            parts += [P(round(ox), round(oy) + 1, 'e'), P(round(ox) - 1, round(oy), 'e')]
    else:
        parts.append(P(round(ox), round(oy), 'd'))
    parts.append(P(round(hx + 3.6 * ca), round(hy + 0.8 + 3.6 * sa), 'n'))
    return parts


def squirrel_run(phase):
    """Running: body horizontal, tail streaming backwards."""
    s = math.sin(phase)
    by = 15.2 - abs(s) * 1.2
    tail_wave = math.cos(phase) * 1.2
    return dict(bx=11.0, by=by, bang=0.0 + s * 0.12, brx=5.4, bry=3.0,
                hx=17.4, hy=by - 2.6, hang=0.15,
                tail=[(6.0, by - 1.0), (3.6, by - 3.0 + tail_wave * 0.3), (2.4, by - 6.0 + tail_wave),
                      (3.0, by - 9.0 + tail_wave), (4.8, by - 10.6 + tail_wave)],
                trad=[2.0, 2.6, 2.8, 2.4, 1.8],
                paws=(14.2, by + 1.6, 15.8 + s * 2.0, 20.0 - max(0.0, s) * 1.5), nut=False,
                foot=(9.2 - s * 2.0, 20.2 - max(0.0, -s) * 1.5, 2.8, 1.0),
                haunch=(8.6, by + 0.6, 3.0, 2.8), belly=False)


def squirrel_back(phase=None, look=0, tail=0.0, stretch=0.0):
    """Squirrel on a trunk seen from behind (dorsal view), head UP: we see the back, the ears,
    four limbs spread and gripping the bark, the bushy tail hanging down. phase = climbing gait
    (diagonal pairs move together), None = clinging still; look = head turned -1 left / 1 right."""
    cx = 12.0
    if phase is None:
        lf = rf = lh = rh = 0.0
        bob = 0.0
    else:
        sw = math.sin(phase)
        lf, rh = sw, sw                       # left front + right hind reach up together
        rf, lh = -sw, -sw
        bob = -abs(math.cos(phase)) * 0.6
    by = 12.4 + bob - stretch
    parts = []
    # tail: hangs below the body, a gentle S that sways with the gait
    tw = tail + (math.sin(phase) * 0.8 if phase is not None else 0.0)
    parts += chain([(cx, by + 4.0), (cx + 1.2 + tw * 0.5, by + 7.0), (cx - 0.4 + tw, by + 9.6),
                    (cx + 0.6 + tw * 1.4, by + 11.4)], [2.2, 2.9, 3.0, 2.2], 'uvx', 0.55)
    # hind legs (thighs spread sideways, feet gripping)
    for sd, ph in ((-1.0, lh), (1.0, rh)):
        hx0, hy0 = cx + sd * 1.8, by + 3.0
        fx, fy = cx + sd * 4.6, by + 4.6 - ph * 1.6
        parts.append(C(hx0, hy0, fx, fy, 1.6, 0.9, 'BAD'))
        parts += [P(round(fx + sd * 0.6), round(fy), 'D'), P(round(fx + sd * 0.6), round(fy) + 1, 'D')]
    # body: a pear seen from behind, darker stripe along the spine
    parts.append(E(cx, by, 3.2, 4.8, 0, 'bad', 0.2))
    parts.append(E(cx, by + 2.4, 3.6, 2.8, 0, 'bad', 0.15))
    parts.append(C(cx, by - 3.2, cx, by + 3.6, 0.5, 0.5, 'aaa'))
    # front legs: from the shoulders up and out, paws hooked into the bark
    for sd, ph in ((-1.0, lf), (1.0, rf)):
        sx0, sy0 = cx + sd * 1.8, by - 2.6
        px, py = cx + sd * 4.0, by - 5.0 - ph * 1.6
        parts.append(C(sx0, sy0, px, py, 1.1, 0.8, 'BAD'))
        parts.append(P(round(px + sd * 0.5), round(py) - 1, 'D'))
    # head from behind (round), slightly turned when looking
    hx, hy = cx + look * 0.8, by - 6.4
    parts.append(E(hx, hy, 2.6, 2.4, 0, 'bad', 0.1))
    for sd in (-1.0, 1.0):
        ex0 = hx + sd * 1.4 + look * 0.3
        parts.append(C(ex0, hy - 1.4, ex0 + sd * 0.5, hy - 3.8, 0.9, 0.45, 'BAD'))
        parts.append(P(round(ex0 + sd * 0.5), round(hy - 4.4), 'D'))
    if look:
        # profile of the face peeking out to the side: eye + nose
        ox = round(hx + look * 2.0)
        parts += [P(ox, round(hy), 'e'), P(ox + (1 if look > 0 else -1), round(hy) + 1, 'n')]
        parts.append(P(ox, round(hy) - 1, 'w'))
    return parts


SQUIRREL_POSES = [
    ('sit', {}),
    ('sit_blink', dict(eye=0)),
    ('eat', dict(paws=(14.4, 12.2, 16.0, 10.2), hang=0.35)),
    ('eat2', dict(paws=(14.4, 12.2, 16.2, 10.8), hang=0.45, eye=0)),
    ('look', dict(hx=13.0, hang=math.pi - 0.2, paws=(14.0, 13.0, 15.2, 14.6), nut=False)),
    ('alert', dict(by=13.8, bry=5.6, bang=-0.4, hx=14.2, hy=6.8, hang=0.05, paws=(13.6, 11.4, 14.6, 12.8),
                   nut=False, haunch=(10.6, 16.6, 3.4, 3.6))),
    ('flick1', dict(tail=[(6.8, 17.0), (4.0, 13.8), (2.8, 9.6), (3.4, 5.6), (5.4, 3.0)],
                    trad=[2.8, 3.2, 3.2, 2.8, 2.2], nut=False, paws=(14.0, 13.0, 15.2, 14.6))),
    ('flick2', dict(tail=[(6.8, 17.0), (5.0, 13.0), (5.4, 8.6), (7.6, 5.2), (10.4, 4.0)],
                    trad=[2.8, 3.2, 3.0, 2.6, 2.0], nut=False, paws=(14.0, 13.0, 15.2, 14.6))),
]


def squirrel(out):
    out.append(SQUIRREL_HEAD)
    for name, pose in SQUIRREL_POSES:
        emit_frame(out, name, squirrel_parts(pose), 24, 22)
    for i in range(4):
        emit_frame(out, 'run%d' % i, squirrel_parts(squirrel_run(i * math.pi / 2)), 24, 22)
    # climbing: seen from behind, head up; going down head first (the same rotated 180°, tail up)
    for i in range(4):
        emit_frame(out, 'climb%d' % i, squirrel_back(i * math.pi / 2), 24, 26, at=(12, 13))
    for i in range(4):
        emit_frame(out, 'down%d' % i, squirrel_back(i * math.pi / 2), 24, 26, at=(12, 13),
                   rot=math.pi, pivot=(12, 13))
    # clinging to the bark: still, the head turns to the sides, the tail twitches
    for name, kw in (('cling', {}), ('cling_l', dict(look=-1)), ('cling_r', dict(look=1)),
                     ('cling_t1', dict(tail=-1.2)), ('cling_t2', dict(tail=1.2)), ('cling_up', dict(stretch=0.8))):
        emit_frame(out, name, squirrel_back(None, **kw), 24, 26, at=(12, 13))
    out.append('''
clip idle loop fps=5 : sit*20 sit_blink sit*12 flick1 flick2 flick1 sit*8 look*6 sit*6
clip eat loop fps=8 : eat eat2 eat eat2 eat eat eat2 eat
clip look loop fps=4 : look*8 sit*2
clip alert loop fps=4 : alert*10 sit_blink alert*4
clip flick once fps=10 : sit flick1 flick2 flick1 flick2 flick1 sit
clip run loop fps=14 speed=55 : run0 run1 run2 run3
clip move loop fps=10 speed=24 : run0 run1 run2 run3
clip climb loop fps=10 speed=30 : climb0 climb1 climb2 climb3
clip climb_down loop fps=8 speed=22 : down0 down1 down2 down3
clip cling loop fps=5 : cling*4 cling_r*5 cling*2 cling_t1 cling_t2 cling_t1 cling*3 cling_l*5 cling_up*2 cling*2
''')
    out.append(SQUIRREL_TAIL)


# ==========================================================================
# FOX
# ==========================================================================

FOX_HEAD = '''# fox — generated by tools/sketch_rig.py
sprite fox
tags creature ground medium mammal predator
anchor 18 23
palette
  b a8673c ~5
  a 864c2a ~5
  d 5a301a ~3
  B a8673c ~5 z+1
  A 864c2a ~5 z+1
  D 5a301a ~3 z+1
  g 6a3a20 ~3 z-2
  f 4e2a16 ~3 z-2
  F 341c10 ~2 z-2
  c d0c6b6 ~4
  C a89e90 ~3
  k 2a1e18 ~2 z+1
  K 1c1410 ~2 z+1
  j 241a14 ~2 z-2
  J 18110d z-2
  e 0c0a08
  E e8b84a glow
  n 141010
end
'''

FOX_TAIL = '''
creature
  move ground
  lane 0.3
  speed walk=14 run=60
  anim idle=idle move=walk run=trot look=look alert=alert eat=sniff sniff=sniff rest=sit sit=sit play=pounce
  intents wander=4 sniff=4 look=3 rest=2 chase=3 leave=2
  homes edge
  fear 0.2
  curiosity 0.6
  size medium
  chases rabbit mouse mole
  scares rabbit squirrel mouse hedgehog frog mole
  point head 11 -15
end
end
'''


def fox_parts(pose):
    p = dict(by=13.4, hx=29.2, hy=8.2, hang=0.08, neck=(23.0, 12.0, 27.2, 9.0),
             legs=dict(nf=(25.0, 15.5, 25.4, 22.6), ff=(26.6, 15.5, 27.0, 22.6),
                       nh=(11.0, 16.0, 12.0, 22.6), fh=(9.4, 16.0, 9.0, 22.6)),
             tail=[(7.2, 12.6), (4.4, 13.8), (2.6, 16.0), (2.0, 18.8)], trad=[2.0, 2.6, 2.6, 1.9],
             tailtip=True, eye=1, sit=False, ears=0.0)
    p.update(pose)
    by = p['by']
    parts = []
    L = p['legs']
    parts += chain(p['tail'], p['trad'], 'gfF', 0.45)
    if p['tailtip']:
        tx, ty = p['tail'][-1]
        parts.append(E(tx, ty + 0.6, 1.6, 1.4, 0, 'cCC', 0.4))
    # far legs (dark)
    for k in ('ff', 'fh'):
        if L.get(k):
            x0, y0, x1, y1 = L[k]
            parts.append(C(x0, y0, x1, y1, 1.1, 0.8, 'gfF'))
            parts.append(C(x0 + (x1 - x0) * 0.6, y0 + (y1 - y0) * 0.6, x1, y1, 0.9, 0.8, 'jJJ'))
    hx, hy, ha = p['hx'], p['hy'], p['hang']
    ca, sa = math.cos(ha), math.sin(ha)
    parts.append(C(hx + 0.6, hy - 1.8, hx + 0.8 + p['ears'], hy - 5.6, 1.3, 0.4, 'gfF'))
    if p['sit']:
        parts.append(E(15.0, 15.6, 6.4, 5.6, -0.6, 'bad', 0.1))
        parts.append(E(12.0, 18.6, 5.0, 3.6, 0, 'bad', 0.1))
        parts.append(E(19.4, 14.2, 2.6, 4.4, -0.3, 'cCa', 0.2))
    else:
        parts.append(E(17.0, by, 9.0, 4.0, 0, 'bad', 0.12))
        parts.append(E(10.6, by + 0.4, 4.2, 4.4, 0, 'bad', 0.1))
        parts.append(E(25.0, by + 1.4, 3.0, 3.4, 0, 'cCa', 0.2))
    x0, y0, x1, y1 = p['neck']
    parts.append(C(x0, y0, x1, y1, 3.0, 2.3, 'bad', 0.1))
    parts.append(E(hx, hy, 3.2, 2.6, ha, 'bad'))
    sx, sy = hx + 1.6 * ca - 0.0, hy + 1.0 + 1.6 * sa
    parts.append(C(sx, sy, hx + 5.4 * ca, hy + 1.8 + 5.4 * sa, 1.6, 0.7, 'bad'))
    parts.append(E(hx + 1.0 * ca, hy + 2.0 + 1.0 * sa, 2.0, 1.1, ha, 'cCC'))
    # near legs with "socks"
    for k in ('nf', 'nh'):
        if L.get(k):
            x0, y0, x1, y1 = L[k]
            parts.append(C(x0, y0, x1, y1, 1.3, 0.9, 'BAD'))
            parts.append(C(x0 + (x1 - x0) * 0.55, y0 + (y1 - y0) * 0.55, x1, y1, 1.0, 0.9, 'kKK'))
    parts.append(C(hx - 0.8, hy - 1.8, hx - 1.0 + p['ears'], hy - 5.8, 1.4, 0.4, 'BAD'))
    parts.append(P(round(hx - 1.0 + p['ears']), round(hy - 3.2), 'k'))
    ox, oy = hx + 0.6 * ca + 0.5 * sa, hy - 0.6 + 0.6 * sa
    if p['eye'] == 1:
        parts += [P(round(ox), round(oy), 'E'), P(round(ox) + 1, round(oy), 'e')]
    else:
        parts.append(P(round(ox), round(oy), 'd'))
    parts.append(P(round(hx + 5.6 * ca), round(hy + 1.6 + 5.6 * sa), 'n'))
    return parts


def fox_walk(phase, trot=False):
    sw = 0.5 if trot else 0.38
    lift = 2.0 if trot else 1.3
    bob = abs(math.sin(phase)) * (0.8 if trot else 0.4)
    legs = {}
    for k, hip, off in (('nf', (25.0, 15.5), 0.0), ('ff', (26.6, 15.5), math.pi),
                        ('nh', (11.0, 16.0), math.pi), ('fh', (9.4, 16.0), 0.0)):
        fx, fy = legs_walk(phase + off, hip[0], hip[1] - bob, 22.6, 7.2, sw, lift)
        legs[k] = (hip[0], hip[1] - bob, fx, fy)
    tw = math.sin(phase) * 0.6
    return dict(by=13.4 - bob, hy=8.2 - bob, neck=(23.0, 12.0 - bob, 27.2, 9.0 - bob), legs=legs,
                tail=[(7.2, 12.6 - bob), (4.4, 13.6 - bob + tw * 0.4), (2.4, 15.4 + tw), (1.6, 17.8 + tw)])


def fox(out):
    out.append(FOX_HEAD)
    W, H = 38, 24
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('look', dict(hx=28.4, hy=6.6, hang=-0.25, neck=(23.0, 12.0, 27.0, 7.8))),
        ('alert', dict(hx=28.0, hy=5.6, hang=0.0, neck=(23.0, 11.6, 26.6, 6.8), ears=-0.4)),
        ('sniff', dict(hx=29.0, hy=16.4, hang=0.9, neck=(23.0, 12.6, 27.4, 15.0))),
        ('sniff2', dict(hx=29.4, hy=17.0, hang=1.0, neck=(23.0, 12.6, 27.6, 15.4))),
        ('sit', dict(sit=True, hx=22.4, hy=6.6, hang=0.05, neck=(19.4, 12.0, 21.4, 7.6),
                     legs=dict(nf=(21.0, 13.0, 21.6, 22.6), ff=(22.4, 13.0, 23.0, 22.6), nh=None, fh=None),
                     tail=[(8.0, 21.0), (12.0, 22.0), (16.0, 22.4), (19.0, 22.2)], trad=[2.0, 2.2, 2.0, 1.6])),
        ('sit_blink', dict(sit=True, hx=22.4, hy=6.6, hang=0.05, neck=(19.4, 12.0, 21.4, 7.6), eye=0,
                           legs=dict(nf=(21.0, 13.0, 21.6, 22.6), ff=(22.4, 13.0, 23.0, 22.6), nh=None, fh=None),
                           tail=[(8.0, 21.0), (12.0, 22.0), (16.0, 22.4), (19.0, 22.2)], trad=[2.0, 2.2, 2.0, 1.6])),
    ]
    for name, pose in poses:
        emit_frame(out, name, fox_parts(pose), W, H)
    # --- the pounce: crouch with a wiggling rump, leap, nose-first landing (the classic fox-on-a-mouse) ---
    crouch = dict(by=17.4, hx=28.6, hy=16.0, hang=0.4, neck=(23.0, 16.0, 26.8, 16.4), ears=-0.5,
                  legs=dict(nf=(25.0, 19.2, 27.8, 22.6), ff=(26.6, 19.2, 29.2, 22.6),
                            nh=(11.0, 19.6, 13.2, 22.6), fh=(9.4, 19.6, 11.4, 22.6)),
                  tail=[(7.2, 16.8), (4.2, 17.4), (1.8, 17.8), (0.2, 18.2)], trad=[2.0, 2.4, 2.2, 1.7])
    wiggle1 = dict(crouch, by=16.4, tail=[(7.2, 15.8), (4.4, 14.6), (2.8, 13.2), (2.0, 11.6)],
                   legs=dict(crouch['legs'], nh=(11.0, 18.6, 11.6, 22.6), fh=(9.4, 18.6, 9.6, 22.6)))
    wiggle2 = dict(crouch, by=16.4, tail=[(7.2, 15.8), (4.0, 15.8), (1.6, 14.8), (0.2, 13.4)],
                   legs=dict(crouch['legs'], nh=(11.0, 18.6, 12.8, 22.6), fh=(9.4, 18.6, 11.0, 22.6)))
    leap = dict(by=8.6, hx=30.4, hy=11.4, hang=1.0, neck=(23.0, 10.0, 28.0, 11.2), ears=-0.4, eye=0,
                legs=dict(nf=(25.0, 12.0, 30.0, 18.2), ff=(26.6, 12.0, 31.4, 19.0),
                          nh=(11.0, 12.4, 5.6, 16.6), fh=(9.4, 12.4, 4.0, 15.4)),
                tail=[(7.2, 7.8), (4.4, 6.4), (2.6, 4.6), (2.0, 2.6)], trad=[2.0, 2.4, 2.2, 1.7])
    land = dict(by=14.2, hx=31.0, hy=18.6, hang=1.25, neck=(23.0, 13.2, 28.6, 17.6), ears=-0.6, eye=0,
                legs=dict(nf=(25.0, 16.4, 28.8, 22.6), ff=(26.6, 16.4, 30.4, 22.6),
                          nh=(11.0, 15.8, 12.0, 22.6), fh=(9.4, 15.8, 9.4, 22.6)),
                tail=[(7.2, 11.4), (4.4, 9.8), (2.8, 8.0), (2.2, 6.2)], trad=[2.0, 2.4, 2.2, 1.7])
    shake = dict(land, hy=17.6, hang=1.0, neck=(23.0, 13.2, 28.0, 16.4), eye=1)
    for name, pose in (('pcrouch', crouch), ('pwiggle1', wiggle1), ('pwiggle2', wiggle2), ('pleap', leap),
                       ('pland', land), ('pland2', dict(land, tail=[(7.2, 11.8), (4.0, 11.0), (1.8, 10.2), (0.4, 9.0)])),
                       ('pshake', shake)):
        emit_frame(out, name, fox_parts(pose), W, H)
    for i in range(6):
        emit_frame(out, 'walk%d' % i, fox_parts(fox_walk(i * math.pi / 3)), W, H)
    for i in range(4):
        emit_frame(out, 'trot%d' % i, fox_parts(fox_walk(i * math.pi / 2, True)), W, H)
    out.append('''
clip pounce loop fps=8 : pcrouch*4 pwiggle1 pwiggle2 pwiggle1 pwiggle2 pwiggle1 pwiggle2 pcrouch pleap pleap pland*3 pland2 pland*2 pshake pland stand*3
clip idle loop fps=5 : stand*18 stand_blink stand*10 look*8 stand*6
clip look loop fps=4 : look*10 stand_blink look*4
clip alert loop fps=4 : alert*10
clip sniff loop fps=6 : sniff sniff2 sniff sniff2 sniff*3 sniff2
clip sit loop fps=4 : sit*14 sit_blink sit*10
clip walk loop fps=9 speed=14 : walk0 walk1 walk2 walk3 walk4 walk5
clip trot loop fps=12 speed=60 : trot0 trot1 trot2 trot3
''')
    out.append(FOX_TAIL)


# ==========================================================================
# OWL (front view)
# ==========================================================================

OWL_HEAD = '''# owl — generated by tools/sketch_rig.py (front view)
sprite owl
tags creature air perch medium bird
anchor 9 21
palette
  b 7a6c5a ~6
  a 5a4e40 ~5
  d 3a3228 ~3
  B 7a6c5a ~6 z+1
  A 5a4e40 ~5 z+1
  D 3a3228 ~3 z+1
  c a89c86 ~5 z+1
  C 8a7e6a ~4 z+1
  E f0b040 glow
  e 0c0806
  k 3a3020
  K 6a5a3a
  s 2e2820
  h 141210
  f 5a4e40 ~5 z-2
  g 3a3228 ~3 z-2
  m 9a7452 ~3 z+2
  n 5a4030 ~2 z+2
  y d49a92 z+2
  Y 8a5c54 z+2
end
'''

OWL_TAIL = '''
creature
  move air perch
  speed walk=30 run=60
  anim idle=idle move=fly run=fly look=look alert=alert rest=sleep hollow=in_hollow hollow_peek=hollow_peek swoop=swoop strike=strike carry=carry
  intents perch=5 roost=5 look=4 rest=2 leave=1 hunt=3
  hunts mouse
  homes branch hollow
  fear 0.1
  curiosity 0.4
  size medium
  point head 0 -13
end
end
'''


def owl_parts(pose):
    p = dict(look=0, blink=0, turn=0.0, wide=0.0, tufts=0.0)
    p.update(pose)
    t = p['turn']
    w = p['wide']
    parts = [
        E(9.0, 14.0, 5.4 + w, 6.0, 0, 'bad', 0.2),
        E(4.2 - w, 14.4, 2.2, 5.0, 0.12, 'BAD', 0.1),
        E(13.8 + w, 14.4, 2.2, 5.0, -0.12, 'BAD', 0.1),
        E(9.0, 15.0, 3.2, 4.0, 0, 'cCC', 0.25),
    ]
    # spots on the chest
    for (x, y) in ((8, 13), (10, 14), (7, 15), (9, 16), (11, 16), (8, 17), (10, 18)):
        parts.append(P(x, y, 's'))
    hx = 9.0 + t
    parts += [
        C(hx - 3.6, 4.4, hx - 4.6 - p['tufts'], 1.0, 1.1, 0.5, 'bad'),
        C(hx + 3.6, 4.4, hx + 4.6 + p['tufts'], 1.0, 1.1, 0.5, 'bad'),
        E(hx, 7.0, 5.0, 4.2, 0, 'bad', 0.15),
        E(hx - 2.0, 7.4, 2.0, 2.0, 0, 'cCC'),
        E(hx + 2.0, 7.4, 2.0, 2.0, 0, 'cCC'),
    ]
    lx = p['look']
    for ex in (round(hx - 2.0), round(hx + 2.0) - 1):
        if p['blink'] == 0:
            parts += [P(ex, 7, 'E'), P(ex + 1, 7, 'E'), P(ex, 8, 'E'), P(ex + 1, 8, 'E')]
            parts.append(P(ex + (1 if lx > 0 else 0), 7 + (0 if lx == 0 else 1), 'e'))
        elif p['blink'] == 1:
            parts += [P(ex, 7, 'C'), P(ex + 1, 7, 'C'), P(ex, 8, 'E'), P(ex + 1, 8, 'E')]
        else:
            parts += [P(ex, 8, 'd'), P(ex + 1, 8, 'd')]
    parts += [P(round(hx), 9, 'K'), P(round(hx), 10, 'k')]
    for x in (6, 7, 10, 11):
        parts.append(P(x, 20, 'k'))
    return parts


def owl_fly(k, talons=0, mouse=False):
    """Flight from the side (facing right), in the style of the sitting owl: a compact round body,
    a big round head with the facial disc turned towards the viewer (glowing eyes), a short tail,
    broad rounded wings with spread primaries and dark bars. k = 0 up, 1 middle, 2 down, 3 glide.
    The near wing is in front of the body (z+1), the far wing behind it (z-2)."""
    cx, cy = 15.0, 15.0
    parts = []
    # wing pose: (elbow, tip) relative to the shoulder; the tip trails backwards
    W = {0: ((-3.0, -6.5), (-8.5, -12.0)),      # up: raised high above the back
         1: ((-4.5, -2.0), (-11.5, -3.5)),      # middle (upstroke / recovery)
         2: ((-3.4, 3.4), (-8.6, 7.6)),         # down: below the belly
         3: ((-4.6, -1.0), (-12.5, -1.0)),      # glide: level, swept back
         4: ((-4.0, -2.4), (-11.0, -4.4)),      # stoop: folded back, the tips trail upwards
         5: ((-1.2, -6.6), (-4.2, -12.6))}[k]   # strike: braking, flared up and slightly forward
    (ex, ey), (tx, ty) = W

    def wing(dx, dy, keys, stripe, far):
        sx, sy = cx + 1.5 + dx, cy - 2.0 + dy
        exx, eyy = sx + ex, sy + ey
        txx, tyy = sx + tx, sy + ty
        ps = []
        # broad wing: leading edge (arm + hand) + a wide trailing part back towards the body
        ps += chain([(sx, sy), (exx, eyy), (txx, tyy)], [2.6, 2.8, 1.4], keys, 0.15)
        bx, by_ = sx - 3.2, sy + (1.2 if k != 2 else 0.4)
        ps += chain([(bx, by_), ((exx + bx) / 2 - 0.5, (eyy + by_) / 2 + 0.6), (txx + 1.0, tyy + 0.4)],
                    [2.4, 2.6, 1.3], keys, 0.2)
        # primaries: 4 rounded "fingers" at the tip
        ang = math.atan2(tyy - eyy, txx - exx)
        for i in range(4):
            fa = ang + (i - 1.5) * 0.28
            f0x, f0y = exx + (txx - exx) * 0.75, eyy + (tyy - eyy) * 0.75
            ps.append(C(f0x, f0y, f0x + math.cos(fa) * 4.2, f0y + math.sin(fa) * 4.2, 0.75, 0.5, keys))
        if not far:
            # dark bars across the wing + a light leading edge
            for t in (0.35, 0.6, 0.82):
                px, py = sx + (txx - sx) * t, sy + (tyy - sy) * t
                nx, ny = -(tyy - sy), (txx - sx)
                L = math.hypot(nx, ny) or 1.0
                nx, ny = nx / L * 1.8, ny / L * 1.8
                ps.append(C(px - nx, py - ny, px + nx, py + ny, 0.35, 0.35, stripe))
            ps.append(C(sx, sy - 0.8, exx, eyy - 0.8, 0.6, 0.6, 'ccc'))
        return ps

    parts += wing(1.2, -0.8, 'fgg', 'sss', True)               # far wing, behind the body
    # tail: short, slightly fanned
    parts += chain([(cx - 4.8, cy - 0.6), (cx - 7.2, cy - 0.2)], [2.2, 1.6], 'bad', 0.2)
    parts.append(P(round(cx - 7.6), round(cy - 0.2), 's'))
    # body: compact, almost horizontal, lighter spotted belly
    parts.append(E(cx, cy, 6.0, 4.2, -0.12, 'bad', 0.2))
    parts.append(E(cx + 0.8, cy + 1.8, 4.4, 2.4, -0.1, 'cCC', 0.25))
    for (x, y) in ((cx - 1, cy + 1), (cx + 1, cy + 2), (cx + 3, cy + 1), (cx - 2, cy + 3), (cx + 2, cy + 3)):
        parts.append(P(round(x), round(y), 's'))
    # talons
    if talons == 0:
        parts += [P(round(cx + 1), round(cy + 4.4), 'k'), P(round(cx + 2), round(cy + 4.4), 'k')]
    elif talons in (1, 2):
        reach = 1.0 if talons == 1 else 1.35
        for dx0 in (0.0, 1.6):                    # two legs, feathered thighs, bare toes
            x0, y0 = cx + 1.2 + dx0, cy + 3.6
            x1, y1 = (x0 + 5.2, y0 + 2.6) if talons == 1 else (x0 + 3.4 * reach, y0 + 3.8 * reach)
            parts.append(C(x0, y0, x1, y1, 1.0, 0.55, 'cCC'))
            for da in ((0.0, 1.5), (1.3, 1.1), (1.6, -0.2)) if talons == 2 else ((0.0, 1.2), (1.1, 0.7)):
                parts.append(C(x1, y1, x1 + da[0] * 1.3, y1 + da[1] * 1.3, 0.45, 0.3, 'kkk'))
    else:
        for dx0 in (0.0, 1.8):                    # short feathered legs; the toes close around the mouse
            parts.append(C(cx + 1.2 + dx0, cy + 3.6, cx + 1.8 + dx0, cy + 5.6, 0.85, 0.55, 'cCC'))
            parts += [P(round(cx + 1.6 + dx0), round(cy + 6.0), 'k'), P(round(cx + 2.8 + dx0), round(cy + 6.2), 'k')]
    if mouse:
        mx, my = cx + 2.8, cy + 8.0                # the mouse hangs from the talons, head forward
        parts += chain([(mx - 3.2, my + 0.5), (mx - 6.0, my + 2.6), (mx - 8.4, my + 2.4)], [0.6, 0.5, 0.35], 'yYY', 0.1)
        parts += [E(mx, my, 3.8, 2.0, -0.1, 'mmn', 0.1),
                  E(mx + 3.7, my + 0.5, 1.8, 1.4, 0.15, 'mmn', 0.1),
                  P(round(mx + 5.4), round(my + 0.9), 'y'), P(round(mx + 4.4), round(my - 0.2), 'e'),
                  P(round(mx + 2.6), round(my - 1.6), 'y'), P(round(mx + 3.8), round(my - 1.8), 'y'),
                  P(round(mx - 0.6), round(my + 2.2), 'Y'), P(round(mx + 1.6), round(my + 2.3), 'Y'),
                  P(round(mx + 2.4), round(my + 1.4), 'Y')]
    # head: big and round, the disc and eyes towards the viewer (like the sitting owl)
    hx, hy = cx + 6.2, cy - 4.0
    parts += [C(hx - 2.8, hy - 2.8, hx - 3.4, hy - 4.6, 0.9, 0.45, 'bad'),
              C(hx + 2.4, hy - 2.8, hx + 3.0, hy - 4.6, 0.9, 0.45, 'bad'),
              E(hx, hy, 4.3, 3.8, 0, 'bad', 0.15),
              E(hx - 1.5, hy + 0.3, 1.8, 1.8, 0, 'cCC'), E(hx + 2.1, hy + 0.3, 1.8, 1.8, 0, 'cCC')]
    for ex_ in (round(hx - 2.2), round(hx + 1.4)):
        parts += [P(ex_, round(hy), 'E'), P(ex_ + 1, round(hy), 'E'), P(ex_, round(hy) + 1, 'E'),
                  P(ex_ + 1, round(hy) + 1, 'E'), P(ex_ + 1, round(hy), 'e')]
    parts += [P(round(hx), round(hy) + 2, 'K'), P(round(hx), round(hy) + 3, 'k')]
    # near wing last: in front of the body
    parts += wing(0.0, 0.0, 'BAD', 'sss', False)
    return parts


def owl(out):
    out.append(OWL_HEAD)
    poses = [
        ('perch', {}),
        ('blink_half', dict(blink=1)),
        ('blink', dict(blink=2)),
        ('look_l', dict(look=-1, turn=-1.0)),
        ('look_r', dict(look=1, turn=1.0)),
        ('ruffle', dict(wide=1.2, tufts=0.6)),
        ('sleep', dict(blink=2, tufts=-0.5)),
    ]
    for name, pose in poses:
        emit_frame(out, name, owl_parts(pose), 18, 22)
    for k in range(4):
        emit_frame(out, 'fly%d' % k, owl_fly(k), 30, 28, at=(15, 15))
    # hunt: stoop (nose down, wings folded back, talons reaching), strike (flared, talons spread),
    # and flapping away with a mouse in the talons
    emit_frame(out, 'stoop', owl_fly(4, talons=1), 30, 28, at=(15, 15), rot=0.42, pivot=(15, 15))
    emit_frame(out, 'strike', owl_fly(5, talons=2), 30, 28, at=(15, 15), rot=-0.18, pivot=(15, 15))
    for k in range(4):
        emit_frame(out, 'carry%d' % k, owl_fly(k, talons=3, mouse=True), 30, 28, at=(15, 15))

    # --- in a hollow: the anchor = the middle between the eyes so they can be placed exactly in the opening ---
    def hollow_eyes(look=0, blink=0):
        ps = [E(9.0, 8.0, 3.2, 2.4, 0, 'hhh')]          # almost black shadow around the eyes (only in the opening)
        for ex in (7, 10):
            if blink == 0:
                ps += [P(ex, 7, 'E'), P(ex + 1, 7, 'E'), P(ex, 8, 'E'), P(ex + 1, 8, 'E')]
                if look:
                    ps.append(P(ex + (1 if look > 0 else 0), 8, 'e'))
            elif blink == 1:
                ps += [P(ex, 8, 'E'), P(ex + 1, 8, 'E')]
        return ps

    def head_only(look=0, blink=0):
        hx = 9.0
        ps = [C(hx - 3.6, 4.4, hx - 4.6, 1.0, 1.1, 0.5, 'bad'), C(hx + 3.6, 4.4, hx + 4.6, 1.0, 1.1, 0.5, 'bad'),
              E(hx, 7.0, 5.0, 4.2, 0, 'bad', 0.15), E(hx - 2.0, 7.4, 2.0, 2.0, 0, 'cCC'), E(hx + 2.0, 7.4, 2.0, 2.0, 0, 'cCC')]
        for ex in (7, 10):
            if blink == 0:
                ps += [P(ex, 7, 'E'), P(ex + 1, 7, 'E'), P(ex, 8, 'E'), P(ex + 1, 8, 'E')]
                ps.append(P(ex + (1 if look > 0 else 0), 7 + (0 if look == 0 else 1), 'e'))
            else:
                ps += [P(ex, 8, 'd'), P(ex + 1, 8, 'd')]
        ps += [P(9, 9, 'K'), P(9, 10, 'k')]
        return ps

    for name, fn, kw in (('hol_eyes', hollow_eyes, {}), ('hol_half', hollow_eyes, dict(blink=1)),
                         ('hol_closed', hollow_eyes, dict(blink=2)), ('hol_l', hollow_eyes, dict(look=-1)),
                         ('hol_r', hollow_eyes, dict(look=1)), ('peek', head_only, {}),
                         ('peek_l', head_only, dict(look=-1)), ('peek_r', head_only, dict(look=1)),
                         ('peek_blink', head_only, dict(blink=1))):
        emit_frame(out, name, fn(**kw), 18, 13, at=(9, 7))
    out.append('''
clip in_hollow loop fps=4 : hol_eyes*16 hol_half hol_closed hol_half hol_eyes*10 hol_l*6 hol_eyes*4 hol_r*6 hol_eyes*6 hol_half hol_eyes*4
clip hollow_peek loop fps=4 : peek*8 peek_l*5 peek*3 peek_r*5 peek_blink peek*4
''')
    out.append('''
clip idle loop fps=4 : perch*16 blink_half blink blink_half perch*10 look_l*8 perch*4 look_r*8 perch*6
clip look loop fps=4 : look_l*8 perch*2 look_r*8 perch*2
clip alert loop fps=4 : ruffle*3 perch*10
clip sleep loop fps=2 : sleep*20 blink_half sleep*10
clip fly loop fps=8 speed=30 : fly0 fly1 fly2 fly1 fly0 fly1 fly2 fly1 fly3*6 fly1
clip swoop loop fps=8 : stoop
clip strike loop fps=8 : strike
clip carry loop fps=8 speed=30 : carry0 carry1 carry2 carry1 carry0 carry1 carry2 carry1 carry3*4 carry1
''')
    out.append(OWL_TAIL)


# ==========================================================================
# HEDGEHOG
# ==========================================================================

HEDGEHOG_HEAD = '''# hedgehog — generated by tools/sketch_rig.py
sprite hedgehog
tags creature ground small mammal slow
anchor 9 11
palette
  s 8a7c6a ~10 z+1
  S 4a4036 ~6
  z 2e2620 ~4
  c 8c7a66 ~4
  C 6a5a4a ~3
  y 4a3c30 ~3
  B 5a4a3c z+1
  A 4a3c30 z+1
  D 2e2620 z+1
  g 3a3028 z-2
  f 2e2620 z-2
  F 201a16 z-2
  e 060504
  w c8d0d8 glow
  n 141010
end
'''

HEDGEHOG_TAIL = '''
creature
  move ground
  lane 0.55
  speed walk=7 run=14
  anim idle=idle move=walk run=walk look=sniff eat=sniff freeze=curl alert=curl sniff=sniff
  intents forage=6 wander=4 rest=1 leave=2
  homes log bush
  fear 0.3
  curiosity 0.3
  size small
  point head 6 -4
end
end
'''


def hedgehog_parts(pose):
    p = dict(curl=False, sx=17.4, sy=8.6, step=0, eye=1, breathe=0.0)
    p.update(pose)
    parts = []
    if p['curl']:
        b = p['breathe']
        parts.append(E(9.0, 7.2 - b * 0.4, 6.0 + b * 0.3, 4.6 + b * 0.4, 0, 'sSz', 0.7))
        parts.append(E(9.0, 7.6, 5.0, 3.8, 0, 'sSz', 0.3))
        return parts
    st = p['step']
    parts.append(C(6.0 - st * 0.8, 8.6, 5.6 - st * 0.8, 11.0, 0.8, 0.7, 'gfF'))
    parts.append(C(13.0 + st * 0.8, 8.6, 13.4 + st * 0.8, 11.0, 0.8, 0.7, 'gfF'))
    parts.append(E(9.6, 8.6, 5.4, 2.2, 0, 'cCy'))
    parts.append(E(14.6, 7.8, 2.6, 2.0, 0.15, 'cCy'))
    parts.append(C(15.4, 8.2, p['sx'], p['sy'], 1.2, 0.6, 'cCy'))
    parts.append(E(8.4, 6.0, 6.6, 4.6, -0.05, 'sSz', 0.75))
    parts.append(E(8.0, 6.4, 5.6, 3.8, 0, 'sSz', 0.3))
    parts.append(C(7.0 + st * 0.8, 9.0, 7.0 + st * 0.8, 11.0, 0.8, 0.7, 'BAD'))
    parts.append(C(12.0 - st * 0.8, 9.0, 12.0 - st * 0.8, 11.0, 0.8, 0.7, 'BAD'))
    if p['eye']:
        parts += [P(15, 7, 'e'), P(16, 7, 'w')]
    else:
        parts.append(P(15, 7, 'y'))
    parts.append(P(round(p['sx']) + 1, round(p['sy']), 'n'))
    return parts


def hedgehog(out):
    out.append(HEDGEHOG_HEAD)
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('walk0', dict(step=1)),
        ('walk1', dict(step=-1)),
        ('sniff', dict(sx=17.6, sy=10.4)),
        ('sniff2', dict(sx=18.0, sy=9.8)),
        ('curl', dict(curl=True)),
        ('curl2', dict(curl=True, breathe=1.0)),
    ]
    for name, pose in poses:
        emit_frame(out, name, hedgehog_parts(pose), 20, 12)
    out.append('''
clip idle loop fps=5 : stand*14 stand_blink stand*8 sniff sniff2 sniff sniff2 stand*6
clip walk loop fps=6 speed=7 : walk0 stand walk1 stand
clip sniff loop fps=7 : sniff sniff2 sniff sniff2 sniff sniff stand
clip curl loop fps=2 : curl curl2
''')
    out.append(HEDGEHOG_TAIL)


# ==========================================================================
# MOUSE
# ==========================================================================

MOUSE_HEAD = '''# mouse — generated by tools/sketch_rig.py
sprite mouse
tags creature ground tiny mammal shy
anchor 7 8
palette
  b 7a6a5a ~4
  a 5a4c40 ~3
  d 3a3028 ~2
  B 7a6a5a z+1
  A 5a4c40 z+1
  D 3a3028 z+1
  p 8a6660
  q 6a4c48
  c 9a8c7a
  e 060504
  w d8e0e8 glow
  n 9a6c66
  t 6a5850
end
'''

MOUSE_TAIL = '''
creature
  move ground
  lane 0.6
  speed walk=14 run=48
  anim idle=idle move=run run=run look=sniff eat=eat sniff=sniff groom=groom freeze=sit
  intents forage=6 wander=4 hide=3 leave=2
  homes log bush hollow
  fear 0.8
  curiosity 0.6
  size tiny
  point head 3 -3
end
end
'''


def mouse_parts(pose):
    p = dict(bx=6.0, by=6.0, brx=3.4, bry=2.3, hx=9.6, hy=5.0, ha=0.15, eye=1, up=False,
             feet=(5.0, 8.0, 8.0, 8.0), tail=[(2.8, 6.8), (1.2, 7.4), (0.0, 6.8)])
    p.update(pose)
    parts = []
    pts = p['tail']
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        parts.append(C(x0, y0, x1, y1, 0.45, 0.4, 'ttt'))
    parts.append(E(p['bx'], p['by'], p['brx'], p['bry'], -0.1 if not p['up'] else -0.9, 'bad', 0.15))
    parts.append(E(p['hx'], p['hy'], 2.2, 1.7, p['ha'], 'bad'))
    ca, sa = math.cos(p['ha']), math.sin(p['ha'])
    parts.append(C(p['hx'] + 1.0 * ca, p['hy'] + 0.4 + sa, p['hx'] + 2.6 * ca, p['hy'] + 0.8 + 2.6 * sa, 1.0, 0.5, 'bad'))
    parts.append(E(p['hx'] - 0.8, p['hy'] - 1.8, 1.3, 1.3, 0, 'pqq'))
    f1x, f1y, f2x, f2y = p['feet']
    parts += [P(round(f1x), round(f1y), 'D'), P(round(f2x), round(f2y), 'D')]
    if p['eye']:
        parts += [P(round(p['hx'] + 0.6), round(p['hy'] - 0.4), 'e')]
    parts.append(P(round(p['hx'] + 3.2 * ca), round(p['hy'] + 0.6 + 3.2 * sa), 'n'))
    return parts


def mouse(out):
    out.append(MOUSE_HEAD)
    poses = [
        ('sit', {}),
        ('sit_blink', dict(eye=0)),
        ('sniff', dict(hy=4.2, ha=-0.15)),
        ('sniff2', dict(hy=4.6, ha=0.0)),
        ('eat', dict(hx=9.8, hy=6.0, ha=0.6)),
        ('groom', dict(up=True, bx=5.6, by=5.0, brx=2.6, bry=3.0, hx=7.6, hy=2.6, ha=0.4, eye=0,
                       feet=(5.0, 8.0, 8.0, 5.0))),
        ('run0', dict(bx=6.0, by=5.6, brx=3.8, bry=2.0, hx=10.2, hy=5.0, feet=(3.0, 8.0, 10.0, 8.0),
                      tail=[(2.4, 6.0), (0.8, 5.0), (0.0, 3.6)])),
        ('run1', dict(bx=6.2, by=6.2, brx=3.2, bry=2.3, hx=9.8, hy=5.4, feet=(6.0, 8.0, 7.0, 8.0),
                      tail=[(2.8, 6.6), (1.0, 6.0), (0.0, 5.0)])),
    ]
    for name, pose in poses:
        emit_frame(out, name, mouse_parts(pose), 14, 9)
    out.append('''
clip idle loop fps=6 : sit*10 sniff sniff2 sniff sit*6 sit_blink sit*4 sniff sniff2 sit*4
clip sniff loop fps=8 : sniff sniff2 sniff sniff2 sit
clip eat loop fps=8 : eat sit eat eat sit
clip groom loop fps=8 : groom sit groom groom sit
clip run loop fps=16 speed=48 : run0 run1
''')
    out.append(MOUSE_TAIL)


# ==========================================================================
# FROG
# ==========================================================================

FROG_HEAD = '''# frog (toad) — generated by tools/sketch_rig.py
sprite frog
tags creature ground small amphibian
anchor 7 10
palette
  b 5e6a3a ~6
  a 44502a ~5
  d 2c341c ~3
  B 5e6a3a ~5 z+1
  A 44502a ~4 z+1
  D 2c341c ~3 z+1
  c 8a8a5a ~3
  q b0ac80 z+1
  E d8a838 glow
  e 0a0806
end
'''

FROG_TAIL = '''
creature
  move ground
  lane 0.65
  speed walk=10 run=26
  anim idle=idle move=hop run=hop look=idle eat=croak
  intents wander=3 rest=4 leave=1
  homes stone log
  fear 0.4
  curiosity 0.2
  size small
  point head 3 -6
end
end
'''


def frog_parts(pose):
    p = dict(by=7.0, croak=0.0, eye=1, legs='sit', bang=-0.1)
    p.update(pose)
    by = p['by']
    parts = [E(7.0, by, 4.8, 3.0, p['bang'], 'bad', 0.1)]
    parts.append(E(10.4, by - 0.8, 2.6, 2.0, 0.1, 'bad'))
    parts.append(E(10.0, by - 2.6, 1.4, 1.2, 0, 'bad'))
    if p['croak'] > 0:
        parts.append(E(11.6, by + 1.4, 1.2 + p['croak'], 1.0 + p['croak'] * 0.8, 0, 'qqc'))
    else:
        parts.append(E(10.8, by + 0.8, 1.8, 0.8, 0, 'ccc'))
    if p['legs'] == 'sit':
        parts.append(E(4.2, by + 2.0, 3.0, 1.5, 0, 'BAD'))
        parts.append(C(10.8, by + 1.4, 11.4, 9.6, 0.7, 0.6, 'BAD'))
    else:
        parts.append(C(4.0, by + 1.0, 0.4, by + 3.0, 1.0, 0.7, 'BAD'))
        parts.append(C(11.0, by + 1.0, 13.0, by + 2.4, 0.7, 0.6, 'BAD'))
    if p['eye']:
        parts += [P(10, round(by - 3), 'E'), P(11, round(by - 3), 'e')]
    else:
        parts.append(P(10, round(by - 3), 'd'))
    for (x, y) in ((5, by - 1), (8, by - 2), (6, by + 1)):
        parts.append(P(x, round(y), 'd'))
    return parts


def frog(out):
    out.append(FROG_HEAD)
    poses = [
        ('sit', {}),
        ('sit_blink', dict(eye=0)),
        ('croak1', dict(croak=0.8)),
        ('croak2', dict(croak=1.6)),
        ('hop0', dict(by=5.6, legs='jump', bang=-0.35)),
        ('hop1', dict(by=4.6, legs='jump', bang=0.0)),
    ]
    for name, pose in poses:
        emit_frame(out, name, frog_parts(pose), 15, 11)
    out.append('''
clip idle loop fps=4 : sit*20 sit_blink sit*12 croak1 croak2 croak1 croak2 croak1 sit*10
clip croak loop fps=6 : croak1 croak2 croak1 croak2 sit*4
clip hop loop fps=8 speed=20 : sit hop0 hop1 hop0
''')
    out.append(FROG_TAIL)


# ==========================================================================
# SNAIL, BEETLE, MOTH, BAT, EYES, FOREST SPIRIT
# ==========================================================================

SMALL = '''# small creatures — generated by tools/sketch_rig.py
'''


def snail(out):
    out.append('''sprite snail
tags creature ground tiny slow
anchor 7 7
palette
  h 8a6a44 ~5 z+1
  H 6a4e30 ~4 z+1
  j 3e2c1a ~3 z+1
  s 241a10 z+1
  m 8a8c84 ~4
  M 6a6c66 ~3
  K 4a4c48 ~2
  o 20201e
end
''')
    def parts(stretch=0.0, hide=False, eyes=1.0):
        ps = []
        if not hide:
            ps.append(C(1.5 - stretch, 6.4, 12.0 + stretch, 6.4, 1.1, 1.2, 'mMK'))
            ps.append(C(11.0 + stretch, 5.6, 12.2 + stretch, 5.6 - 2.6 * eyes, 0.45, 0.35, 'mMK'))
            ps.append(C(10.4 + stretch, 5.8, 10.8 + stretch, 5.8 - 2.0 * eyes, 0.45, 0.35, 'MKK'))
            ps.append(P(round(12.4 + stretch), round(5.6 - 2.8 * eyes), 'o'))
        else:
            ps.append(C(3.0, 6.6, 9.6, 6.6, 0.9, 0.9, 'MKK'))
        ps.append(E(6.4, 3.8, 3.6, 3.2, 0, 'hHj'))
        for (x, y) in ((6, 2), (7, 3), (7, 4), (6, 5), (5, 4), (5, 3)):
            ps.append(P(x, y, 's'))
        return ps
    for name, kw in (('crawl0', {}), ('crawl1', dict(stretch=1.0)), ('look', dict(eyes=1.3)),
                     ('hide', dict(hide=True))):
        emit_frame(out, name, parts(**kw), 15, 8)
    out.append('''
clip idle loop fps=2 : crawl0*6 look*4
clip crawl loop fps=2 speed=1.5 : crawl0 crawl1
clip hide loop fps=1 : hide
creature
  move ground
  lane 0.8
  speed walk=1.5 run=1.5
  anim idle=idle move=crawl run=crawl freeze=hide alert=hide
  intents wander=6 rest=2
  fear 0.2
  size tiny
end
end
''')


def beetle(out):
    out.append('''sprite beetle
tags creature ground tiny insect
anchor 4 5
palette
  b 3c4a5a ~6 z+1
  a 263040 ~4 z+1
  d 141a24 ~2 z+1
  l 1a1c20
  w 8aa0b8 glow
end
''')
    def parts(step):
        ps = []
        for i, x in enumerate((2, 4, 6)):
            dx = step if i % 2 == 0 else -step
            ps.append(P(x + dx, 5, 'l'))
        ps += [E(4.0, 3.0, 3.0, 1.8, 0, 'bad'), E(7.4, 3.4, 1.0, 1.0, 0, 'bad'), P(3, 2, 'w')]
        return ps
    emit_frame(out, 'crawl0', parts(0), 9, 6)
    emit_frame(out, 'crawl1', parts(1), 9, 6)
    out.append('''
clip idle loop fps=1 : crawl0
clip crawl loop fps=8 speed=5 : crawl0 crawl1
creature
  move ground
  lane 0.75
  speed walk=5 run=9
  anim idle=idle move=crawl run=crawl
  intents wander=8 leave=1
  fear 0.1
  size tiny
end
end
''')


def moth(out):
    out.append('''sprite moth
tags creature air tiny insect
anchor 6 4
palette
  b b8ae98 ~6 z+1
  a 98907c ~5 z+1
  d 6a6456 ~3 z+1
  k 3a3630 z+1
end
''')
    def parts(k):
        ps = []
        if k == 0:
            ps += [E(4.4, 1.8, 1.6, 2.2, 0.3, 'bad'), E(7.6, 1.8, 1.6, 2.2, -0.3, 'bad')]
        elif k == 1:
            ps += [E(3.6, 3.4, 2.6, 1.6, 0.3, 'bad'), E(8.4, 3.4, 2.6, 1.6, -0.3, 'bad')]
        else:
            ps += [E(4.0, 5.0, 2.2, 1.4, -0.4, 'bad'), E(8.0, 5.0, 2.2, 1.4, 0.4, 'bad')]
        ps.append(C(6.0, 2.4, 6.0, 5.6, 0.6, 0.5, 'kkk'))
        return ps
    for k in range(3):
        emit_frame(out, 'flap%d' % k, parts(k), 13, 8)
    out.append('''
clip idle loop fps=14 : flap0 flap1 flap2 flap1
clip fly loop fps=14 speed=12 : flap0 flap1 flap2 flap1
creature
  move air
  speed walk=10 run=18
  anim idle=idle move=fly run=fly
  intents wander=6 follow=3 leave=1
  attracted glow
  fear 0.1
  size tiny
end
end
''')


def bat(out):
    out.append('''sprite bat
tags creature air small mammal
anchor 10 5
palette
  b 3a3438 ~3 z+1
  a 2a2428 ~2 z+1
  d 1a1618 z+1
  m 46383c ~3 z+1
  e c8a050 glow
end
''')
    def parts(k):
        wy = (-3.6, 0.0, 3.0)[k]
        ps = [
            C(10.0, 4.6, 1.0, 4.6 + wy, 1.6, 0.6, 'mad', 0.3),
            C(10.0, 4.6, 19.0, 4.6 + wy, 1.6, 0.6, 'mad', 0.3),
            C(5.0, 4.6 + wy * 0.5, 2.0, 5.6 + wy, 0.9, 0.5, 'mad', 0.4),
            C(15.0, 4.6 + wy * 0.5, 18.0, 5.6 + wy, 0.9, 0.5, 'mad', 0.4),
            E(10.0, 5.0, 1.8, 2.2, 0, 'bad'),
            C(9.0, 3.0, 8.6, 1.4, 0.5, 0.3, 'bad'), C(11.0, 3.0, 11.4, 1.4, 0.5, 0.3, 'bad'),
            P(9, 4, 'e'), P(11, 4, 'e'),
        ]
        return ps
    for k in range(3):
        emit_frame(out, 'flap%d' % k, parts(k), 21, 10)

    # --- hanging upside down: the anchor = the little claws at the top (they attach to the line under a branch) ---
    def hang(breath=0.0, look=0, wings=0.0, flip=0.0):
        cx = 6.0
        ps = [P(5, 0, 'd'), P(7, 0, 'd'), C(5.4, 0.6, 5.8, 2.4, 0.4, 0.4, 'ddd'), C(6.8, 0.6, 6.4, 2.4, 0.4, 0.4, 'ddd')]
        if wings > 0:
            # spread wings (stretching / transition when landing)
            ps.append(C(cx, 3.0, cx - 4.4 * wings - 1.0, 8.0 - 3.0 * flip, 1.2, 0.6, 'mad', 0.3))
            ps.append(C(cx, 3.0, cx + 4.4 * wings + 1.0, 8.0 - 3.0 * flip, 1.2, 0.6, 'mad', 0.3))
        ps.append(E(cx, 5.4, 2.4 + breath * 0.3, 3.4, 0, 'mad', 0.15))     # folded wings around the body
        ps.append(E(cx, 5.6, 1.6, 2.6, 0, 'bad'))
        hx = cx + look * 0.6
        ps.append(E(hx, 9.4, 1.7, 1.5, 0, 'bad'))                          # head down
        ps += [C(hx - 0.8, 10.4, hx - 1.4, 12.4, 0.5, 0.3, 'bad'), C(hx + 0.8, 10.4, hx + 1.4, 12.4, 0.5, 0.3, 'bad')]
        ps += [P(round(hx - 0.6), 9, 'e'), P(round(hx + 0.8), 9, 'e')]
        return ps
    for name, kw in (('hang0', {}), ('hang1', dict(breath=1.0)), ('hang_l', dict(look=-1)), ('hang_r', dict(look=1)),
                     ('hang_stretch', dict(wings=1.0)), ('land0', dict(wings=1.2, flip=1.0)), ('land1', dict(wings=0.8, flip=0.5))):
        emit_frame(out, name, hang(**kw), 14, 14, at=(6, 0))
    out.append('''
clip idle loop fps=12 : flap0 flap1 flap2 flap1
clip fly loop fps=12 speed=34 : flap0 flap1 flap2 flap1
clip hang loop fps=4 : hang0*10 hang1*4 hang0*8 hang_l*6 hang0*4 hang_r*6 hang0*6 hang_stretch*3 hang1*3 hang0*4
clip land once fps=8 : land0 land1 hang_stretch hang0
clip takeoff once fps=10 : hang0 hang_stretch land1 land0
creature
  move air perch
  speed walk=24 run=40
  anim idle=idle move=fly run=fly hang=hang land=land takeoff=takeoff
  intents wander=4 roost=6 leave=2
  fear 0.2
  size small
  rare 1
end
end
''')


def eyes(out):
    out.append('''sprite eyes
tags creature hidden eyes tiny
anchor 4 1
palette
  E e8b84a glow
  G 9ae070 glow
  e 0a0806 glow
  h 6a5022 glow
  x 0a0a0a
end
''')
    frames = {
        'open':   ['EE...EE', 'EE...EE'],
        'look_l': ['eE...eE', 'EE...EE'],
        'look_r': ['Ee...Ee', 'EE...EE'],
        'half':   ['hh...hh', 'EE...EE'],
        'closed': ['.......', 'hh...hh'],
        'none':   ['x......', '.......'],
    }
    for name, rows in frames.items():
        out.append('frame %s at 3 1' % name)
        for r in rows:
            out.append('|' + r)
        out.append('')
    out.append('''
clip idle loop fps=6 : open*14 half closed half open*10 look_l*6 open*4 look_r*6 open*4
clip blink once fps=10 : open half closed half open
clip appear once fps=6 : none closed half open
clip vanish once fps=6 : open half closed none
clip look loop fps=4 : look_l*6 open*2 look_r*6 open*2
creature
  move hidden
  anim idle=idle look=look appear=appear vanish=vanish
  intents watch=6 follow=3 vanish=2
  homes bush hollow log
  size tiny
  point head 0 0
end
end
''')


def spirit(out):
    """Forest spirit — a mossy creature with lantern eyes (rare, mildly fantasy)."""
    out.append('''sprite spirit
tags creature ground small fantasy rare glow
anchor 8 15
palette
  b 3e6a46 ~8
  a 2a4e34 ~6
  d 1a3424 ~4
  B 4e7a50 ~6 z+1
  A 2e5638 ~5 z+1
  D 1a3424 ~3 z+1
  l 6a9a4a ~6 z+2
  L 3e6a30 ~4 z+2
  k 4a3a2a z+2
  E 9ae8d8 glow
  q 6ad0b0 glow
  e 0a1210
end
''')
    def parts(squash=0.0, blink=False, step=0, look=0):
        ps = [
            C(6.0 + step, 12.0, 5.6 + step, 15.0, 0.8, 0.7, 'aad'),
            C(10.0 - step, 12.0, 10.4 - step, 15.0, 0.8, 0.7, 'aad'),
            E(8.0, 9.0 + squash * 0.5, 5.4 + squash * 0.5, 4.8 - squash * 0.6, 0, 'bad', 0.55),
            E(8.0, 9.6, 4.2, 3.6, 0, 'BAD', 0.4),
            C(8.0, 4.6 + squash, 8.4, 2.0 + squash, 0.4, 0.3, 'kkk'),
            C(8.4, 2.4 + squash, 12.0, 0.8 + squash, 1.4, 0.6, 'lLL'),
        ]
        ey = round(8.6 + squash * 0.5)
        if blink:
            ps += [P(6 + look, ey, 'd'), P(10 + look, ey, 'd')]
        else:
            ps += [P(6 + look, ey, 'E'), P(10 + look, ey, 'E'), P(6 + look, ey + 1, 'q'), P(10 + look, ey + 1, 'q')]
        ps += [P(4, 11, 'q'), P(12, 9, 'q')]
        return ps
    for name, kw in (('idle0', {}), ('idle1', dict(squash=0.8)), ('blink', dict(blink=True)),
                     ('walk0', dict(step=1)), ('walk1', dict(step=-1, squash=0.5)),
                     ('look_l', dict(look=-1)), ('look_r', dict(look=1))):
        emit_frame(out, name, parts(**kw), 16, 16)
    out.append('''
clip idle loop fps=3 : idle0*4 idle1*3 idle0*4 idle1*3 blink idle0*2 look_l*4 idle0 look_r*4
clip walk loop fps=6 speed=8 : walk0 idle0 walk1 idle1
clip look loop fps=3 : look_l*4 idle0 look_r*4 idle0
creature
  move ground
  lane 0.4
  speed walk=8 run=12
  anim idle=idle move=walk run=walk look=look
  intents wander=4 look=4 hide=2 leave=2
  homes bush hollow
  fear 0.3
  curiosity 0.9
  size small
  rare 1
  point head 0 -6
end
end
''')


# ==========================================================================
# QUADRUPEDS — shared skeleton (badger, boar, wildcat)
# ==========================================================================

QUAD_KEYS = 'bad BAD gfF cCa kKK jJJ'


def quad(c, pose):
    """A generic quadruped facing right. c = species configuration, pose = changes."""
    p = dict(c)
    p.update(pose)
    parts = []
    hx, hy, ha = p['hx'], p['hy'], p['hang']
    ca, sa = math.cos(ha), math.sin(ha)
    if p.get('tail'):
        parts += chain(p['tail'], p['trad'], p.get('tailkeys', 'gfF'), p.get('tailfuzz', 0.3))
    for t in p.get('tailextra', []):
        parts.append(t)
    lr0, lr1 = p['legr']
    for k in ('ff', 'fh'):
        L = p['legs'].get(k)
        if L:
            x0, y0, x1, y1 = L
            parts.append(C(x0, y0, x1, y1, lr0, lr1, p.get('farlegkeys', 'gfF')))
            parts.append(C(x0 + (x1 - x0) * 0.6, y0 + (y1 - y0) * 0.6, x1, y1, lr1 * 1.05, lr1, 'jJJ'))
    ex, ey, el, er = p['ear']
    if p.get('earkind') == 'round':
        parts.append(E(hx + ex + 1.2, hy + ey, er, er, 0, 'gfF'))
    else:
        parts.append(C(hx + ex + 1.2, hy + ey, hx + ex + 1.6, hy + ey - el, er, 0.4, 'gfF'))
    for b in p['body']:
        parts.append(E(*b, keys='bad', fuzz=p.get('fuzz', 0.1)))
    if p.get('chest'):
        parts.append(E(*p['chest'], keys='cCa', fuzz=0.2))
    for e in p.get('bodyextra', []):
        parts.append(e)
    if p.get('neck'):
        x0, y0, x1, y1, r0, r1 = p['neck']
        parts.append(C(x0, y0, x1, y1, r0, r1, 'bad', p.get('fuzz', 0.1)))
    hk = p.get('headkeys', 'bad')
    parts.append(E(hx, hy, p['hrx'], p['hry'], ha, hk))
    sl, sr0, sr1 = p['snout']
    sx0, sy0 = hx + p['hrx'] * 0.3 * ca, hy + 0.7 + p['hrx'] * 0.3 * sa
    tx, ty = hx + (p['hrx'] + sl) * ca, hy + 1.0 + (p['hrx'] + sl) * sa
    parts.append(C(sx0, sy0, tx, ty, sr0, sr1, p.get('snoutkeys', hk)))
    if p.get('headextra'):
        parts += p['headextra'](hx, hy, ha, tx, ty)
    for k in ('nf', 'nh'):
        L = p['legs'].get(k)
        if L:
            x0, y0, x1, y1 = L
            parts.append(C(x0, y0, x1, y1, lr0 * 1.1, lr1, p.get('legkeys', 'BAD')))
            parts.append(C(x0 + (x1 - x0) * 0.6, y0 + (y1 - y0) * 0.6, x1, y1, lr1 * 1.1, lr1, 'kKK'))
    if p.get('earkind') == 'round':
        parts.append(E(hx + ex, hy + ey, er, er, 0, p.get('earkeys', 'BAD')))
    else:
        tip = p.get('eartip', 0.4)
        lean = p.get('earlean', -0.4)
        parts.append(C(hx + ex, hy + ey, hx + ex + lean, hy + ey - el, er, tip, p.get('earkeys', 'BAD')))
        if p.get('earinner'):
            # light inner ear
            parts.append(C(hx + ex + 0.2, hy + ey - 0.4, hx + ex + lean + 0.1, hy + ey - el * 0.75,
                           er * 0.45, tip * 0.5, p['earinner']))
    ox, oy = hx + p['hrx'] * 0.35 * ca + 0.5 * sa, hy - 0.6 + p['hrx'] * 0.35 * sa
    if p.get('bigeye'):
        # big dark 2×2 eye with a white highlight; closed = a content little arc
        ix, iy = round(ox), round(oy)
        if p.get('eye', 1):
            parts += [P(ix, iy, 'e'), P(ix + 1, iy, 'e'), P(ix, iy + 1, 'e'), P(ix + 1, iy + 1, 'e'), P(ix, iy, 'w')]
        else:
            parts += [P(ix, iy + 1, 'd'), P(ix + 1, iy + 1, 'd')]
        if p.get('brow'):
            parts.append(P(ix, iy - 1, p['brow']))
    elif p.get('eye', 1):
        if p.get('eyeshine'):
            parts += [P(round(ox), round(oy), 'E'), P(round(ox) + 1, round(oy), 'e')]
        else:
            parts += [P(round(ox), round(oy), 'e'), P(round(ox) + 1, round(oy), 'w')]
    else:
        parts.append(P(round(ox), round(oy), 'd'))
    parts.append(P(round(tx + 0.6 * ca), round(ty + 0.3), 'n'))
    return parts


def quad_walk(c, phase, swing=0.38, lift=1.3, bobamt=0.4, ground=None):
    G = ground if ground is not None else c['G']
    bob = abs(math.sin(phase)) * bobamt
    legs = {}
    for k, off in (('nf', 0.0), ('ff', math.pi), ('nh', math.pi), ('fh', 0.0)):
        hx_, hy_ = c['hips'][k]
        fx, fy = legs_walk(phase + off, hx_, hy_ - bob, G, G - hy_, swing, lift)
        legs[k] = (hx_, hy_ - bob, fx, fy)
    body = [(b[0], b[1] - bob) + tuple(b[2:]) for b in c['body']]
    pose = dict(legs=legs, body=body, hy=c['hy'] - bob)
    if c.get('neck'):
        n = c['neck']
        pose['neck'] = (n[0], n[1] - bob, n[2], n[3] - bob, n[4], n[5])
    if c.get('chest'):
        ch = c['chest']
        pose['chest'] = (ch[0], ch[1] - bob) + tuple(ch[2:])
    if c.get('tail'):
        tw = math.sin(phase) * 0.5
        pose['tail'] = [(x, y - bob + tw * i * 0.3) for i, (x, y) in enumerate(c['tail'])]
    return pose


def stand_legs(c):
    G = c['G']
    return {k: (x, y, x + (0.3 if k[1] == 'f' else -0.2), G) for k, (x, y) in c['hips'].items()}


def emit_quad(out, header, c, poses, walk_frames, w, h, clips, profile, swing=0.38, lift=1.3, bob=0.4):
    out.append(header)
    base = dict(c)
    base['legs'] = stand_legs(c)
    for name, pose in poses:
        pp = dict(base)
        pp.update(pose)
        emit_frame(out, name, quad(pp, {}), w, h)
    for i in range(walk_frames):
        pp = dict(base)
        pp.update(quad_walk(c, i * 2 * math.pi / walk_frames, swing, lift, bob))
        emit_frame(out, 'walk%d' % i, quad(pp, {}), w, h)
    out.append(clips)
    out.append(profile)


def badger(out):
    G = 17.4
    def stripes(hx, hy, ha, tx, ty):
        # black stripe from the nose through the eye to the ear
        return [C(tx - 0.6, ty - 0.9, hx - 2.4, hy - 1.8, 0.6, 0.8, 'SSS')]
    c = dict(G=G, body=[(16.0, 10.2, 10.6, 5.0, 0.0), (8.4, 10.8, 5.0, 4.8, 0.0)],
             hx=27.4, hy=11.4, hang=0.32, hrx=3.4, hry=2.7, snout=(3.0, 1.7, 0.8),
             headkeys='cCC', ear=(-2.2, -2.0, 1.0, 1.0), earkind='round', earkeys='cCC',
             neck=(23.0, 10.0, 25.6, 11.0, 3.6, 3.0), headextra=stripes,
             hips=dict(nf=(23.0, 13.0), ff=(24.4, 13.0), nh=(9.4, 13.0), fh=(7.8, 13.0)),
             legr=(1.6, 1.2), legkeys='kKK', farlegkeys='jJJ',
             tail=[(3.6, 8.6), (1.6, 9.6)], trad=[1.7, 1.2], tailkeys='bad', fuzz=0.22)
    header = """# badger — generated by tools/sketch_rig.py (shared quadruped skeleton)
sprite badger
tags creature ground medium mammal
anchor 17 17
palette
  b 8e8a84 ~6
  a 6c6862 ~5
  d 46423e ~3
  B 8e8a84 ~6 z+1
  A 6c6862 ~5 z+1
  D 46423e ~3 z+1
  g 5a5650 z-2
  f 46423e z-2
  F 302e2a z-2
  c d0ccc4 ~3
  C a8a49c ~3
  S 141414
  k 1e1c1a ~2 z+1
  K 141210 z+1
  j 1a1816 z-2
  J 121010 z-2
  e 060606
  w c8d0d8 glow
  n 0e0c0c
end
"""
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('sniff', dict(hx=27.0, hy=14.0, hang=0.75, neck=(23.0, 10.6, 25.2, 13.0, 3.6, 2.8))),
        ('sniff2', dict(hx=27.4, hy=14.6, hang=0.85, neck=(23.0, 10.6, 25.4, 13.4, 3.6, 2.8))),
        ('look', dict(hx=27.0, hy=9.4, hang=0.0, neck=(23.0, 10.0, 25.2, 9.6, 3.6, 3.0))),
        ('dig', dict(hx=27.0, hy=14.2, hang=0.8, neck=(23.0, 10.6, 25.2, 13.0, 3.6, 2.8),
                     legs=dict(nf=(23.0, 13.0, 26.4, 16.6), ff=(24.4, 13.0, 24.8, G),
                               nh=(9.4, 13.0, 9.0, G), fh=(7.8, 13.0, 7.6, G)))),
    ]
    clips = """
clip idle loop fps=5 : stand*16 stand_blink stand*8 look*6 stand*4
clip sniff loop fps=6 : sniff sniff2 sniff sniff2 sniff*2 stand
clip dig loop fps=8 : dig sniff2 dig sniff2 dig dig sniff
clip look loop fps=4 : look*8 stand_blink look*4
clip walk loop fps=8 speed=9 : walk0 walk1 walk2 walk3 walk4 walk5
clip run loop fps=13 speed=24 : walk0 walk1 walk2 walk3 walk4 walk5
"""
    profile = """creature
  move ground
  lane 0.3
  speed walk=9 run=24
  anim idle=idle move=walk run=run look=look eat=dig sniff=sniff alert=look
  intents forage=6 wander=4 sniff=4 rest=1 hide=1 leave=2
  homes log brushwood edge
  fear 0.2
  curiosity 0.4
  size medium
  point head 11 -7
end
end
"""
    emit_quad(out, header, c, poses, 6, 36, 18, clips, profile, 0.3, 1.0, 0.3)


def boar(out):
    G = 25.4
    def head_extra(hx, hy, ha, tx, ty):
        return [P(round(tx - 1.6), round(ty + 0.6), 'T'), P(round(tx - 2.4), round(ty - 0.4), 'T'),
                E(tx + 0.4, ty, 1.0, 1.4, 0.3, 'nnn')]
    c = dict(G=G, body=[(19.0, 12.6, 12.4, 7.0, -0.08), (25.6, 10.4, 7.0, 7.4, 0.0), (9.8, 13.8, 6.0, 6.4, 0.0)],
             hx=33.0, hy=13.8, hang=0.5, hrx=4.8, hry=4.0, snout=(5.2, 2.6, 1.5),
             ear=(-2.6, -3.0, 3.4, 1.3), headextra=head_extra,
             bodyextra=[C(9.0, 7.0, 29.0, 3.8, 1.3, 1.8, 'zzz', 0.5)],
             hips=dict(nf=(27.0, 17.0), ff=(29.0, 17.0), nh=(11.0, 18.0), fh=(9.0, 18.0)),
             legr=(2.2, 1.0), tail=[(5.0, 9.4), (3.6, 12.0), (3.2, 14.6)], trad=[0.7, 0.6, 1.0],
             tailkeys='zzz', fuzz=0.45)
    header = """# boar — generated by tools/sketch_rig.py (shared quadruped skeleton)
sprite boar
tags creature ground large mammal rare noisy
anchor 21 25
palette
  b 5e5044 ~8
  a 44382e ~6
  d 2a221c ~4
  B 5e5044 ~8 z+1
  A 44382e ~6 z+1
  D 2a221c ~4 z+1
  g 3c3228 ~4 z-2
  f 2c241e ~3 z-2
  F 1e1814 z-2
  c 6e604e ~4
  C 54483a ~3
  z 1c1612 ~4
  k 1a1410 z+1
  K 100c0a z+1
  j 16120e z-2
  J 0e0a08 z-2
  T c8bea8 z+1
  n 6a4e48
  e 060404
  w b0b8c0 glow
end
"""
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('root', dict(hx=33.4, hy=19.0, hang=1.0)),
        ('root2', dict(hx=33.8, hy=19.6, hang=1.1)),
        ('alert', dict(hx=32.6, hy=10.6, hang=0.2)),
    ]
    clips = """
clip idle loop fps=5 : stand*12 stand_blink stand*6 root root2 root root2 stand*4
clip root loop fps=6 : root root2 root root2 root*3 root2
clip alert loop fps=4 : alert*10
clip walk loop fps=8 speed=11 : walk0 walk1 walk2 walk3 walk4 walk5
clip run loop fps=14 speed=40 : walk0 walk1 walk2 walk3 walk4 walk5
"""
    profile = """creature
  move ground
  lane 0.05
  speed walk=11 run=40
  anim idle=idle move=walk run=run eat=root sniff=root alert=alert look=alert
  intents forage=7 wander=3 look=2 leave=3
  homes edge
  fear 0.15
  curiosity 0.3
  size large
  rare 1
  scares rabbit mouse squirrel hedgehog frog weasel badger
  point head 14 -10
end
end
"""
    emit_quad(out, header, c, poses, 6, 44, 26, clips, profile, 0.32, 1.4, 0.4)


def wildcat(out):
    G = 21.4
    def stripes(hx, hy, ha, tx, ty):
        return [C(hx - 1.4, hy - 2.4, hx - 0.6, hy - 0.8, 0.4, 0.4, 'SSS')]
    body_stripes = [C(x, 9.0, x - 0.6, 13.4, 0.45, 0.45, 'SSS') for x in (10.0, 13.0, 16.0, 19.0)]
    ring = [C(x - 1.0, y - 1.6, x + 1.0, y + 1.6, 0.5, 0.5, 'SSS') for (x, y) in ((4.0, 13.4), (2.2, 16.6))]
    c = dict(G=G, body=[(16.0, 12.0, 8.4, 3.8, 0.0), (10.0, 12.6, 4.0, 4.2, 0.0)],
             chest=(22.2, 13.4, 2.4, 3.0, 0.0),
             neck=(21.0, 11.0, 24.4, 8.4, 2.6, 2.2),
             hx=26.0, hy=7.4, hang=0.05, hrx=3.0, hry=2.6, snout=(1.0, 1.5, 1.1),
             ear=(-1.0, -2.0, 2.8, 1.2), headextra=stripes, bodyextra=body_stripes, tailextra=[],
             hips=dict(nf=(21.0, 14.5), ff=(22.4, 14.5), nh=(11.0, 14.5), fh=(9.6, 14.5)),
             legr=(1.3, 0.9), tail=[(8.0, 11.0), (4.8, 12.4), (2.6, 15.0), (1.8, 18.2)], trad=[1.7, 1.8, 1.8, 1.9],
             tailkeys='bad', eyeshine=True, fuzz=0.15)
    c['bodyextra'] = body_stripes + ring
    header = """# wildcat — generated by tools/sketch_rig.py (shared quadruped skeleton)
sprite wildcat
tags creature ground medium mammal predator rare
anchor 17 21
palette
  b 8a7a62 ~6
  a 6a5c48 ~5
  d 463c30 ~3
  B 8a7a62 ~6 z+1
  A 6a5c48 ~5 z+1
  D 463c30 ~3 z+1
  g 54483a z-2
  f 403628 z-2
  F 2a241c z-2
  c a89a84 ~4
  C 887c68 ~3
  S 2a2420 ~2
  k 3a3228 z+1
  K 2a241e z+1
  j 2e2820 z-2
  J 221c18 z-2
  E b8d060 glow
  e 060606
  w c8d0d8 glow
  n 4a3030
end
"""
    sit = dict(body=[(14.0, 12.8, 4.6, 6.0, -0.9), (11.4, 16.8, 5.0, 4.0, 0.0)], chest=(17.0, 12.0, 2.0, 3.6, -0.3),
               neck=(15.4, 9.6, 17.2, 6.6, 2.4, 2.0), hx=18.6, hy=5.4,
               legs=dict(nf=(16.6, 12.0, 17.2, G), ff=(17.8, 12.0, 18.4, G), nh=None, fh=None),
               tail=[(9.0, 20.4), (13.0, 21.0), (17.0, 21.2), (20.4, 20.6)], trad=[1.5, 1.5, 1.5, 1.4],
               bodyextra=[C(x, 9.0, x - 1.4, 14.0, 0.45, 0.45, 'SSS') for x in (12.0, 14.6)])
    stalk = dict(body=[(16.0, 14.4, 8.4, 3.4, 0.05), (10.0, 14.6, 4.0, 3.8, 0.0)], chest=(22.0, 15.4, 2.2, 2.6, 0.0),
                 neck=(21.0, 13.6, 24.6, 13.4, 2.4, 2.0), hx=26.4, hy=13.0, hang=0.1,
                 legs=dict(nf=(21.0, 16.4, 23.4, G), ff=(22.4, 16.4, 24.4, G), nh=(11.0, 16.4, 8.4, G), fh=(9.6, 16.4, 7.4, G)),
                 bodyextra=[C(x, 11.6, x - 0.6, 15.6, 0.45, 0.45, 'SSS') for x in (10.0, 13.0, 16.0, 19.0)])
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('look', dict(hx=25.6, hy=6.8, hang=-0.25)),
        ('sit', sit),
        ('sit_blink', dict(sit, eye=0)),
        ('stalk', stalk),
        ('stalk2', dict(stalk, hy=13.4, hang=0.15)),
    ]
    clips = """
clip idle loop fps=4 : stand*12 stand_blink stand*6 look*6 stand*4
clip look loop fps=4 : look*8 stand_blink look*4
clip sit loop fps=3 : sit*14 sit_blink sit*10
clip stalk loop fps=4 : stalk*6 stalk2*3 stalk*4
clip walk loop fps=8 speed=12 : walk0 walk1 walk2 walk3 walk4 walk5
clip run loop fps=14 speed=50 : walk0 walk1 walk2 walk3 walk4 walk5
"""
    profile = """creature
  move ground
  lane 0.25
  speed walk=12 run=50
  anim idle=idle move=walk run=run look=look alert=stalk rest=sit sit=sit sniff=stalk
  intents wander=4 look=3 rest=3 sniff=2 leave=3
  homes log edge
  fear 0.3
  curiosity 0.6
  size medium
  rare 1
  scares rabbit mouse squirrel frog weasel
  point head 10 -14
end
end
"""
    emit_quad(out, header, c, poses, 6, 34, 22, clips, profile, 0.4, 1.4, 0.35)


# ==========================================================================
# WEASEL, DORMOUSE, MOLE, SALAMANDER, SLUG, STAG BEETLE, SPIDER
# ==========================================================================

def weasel(out):
    out.append("""# weasel — generated by tools/sketch_rig.py
sprite weasel
tags creature ground small mammal predator
anchor 12 13
palette
  b 8a5c36 ~5
  a 6a4428 ~4
  d 44291a ~3
  B 8a5c36 ~5 z+1
  A 6a4428 ~4 z+1
  D 44291a ~3 z+1
  g 54361f z-2
  f 3e2616 z-2
  F 2a1a10 z-2
  c c8bca8 ~3
  C a89c88 ~3
  t 2a1a10
  e 060404
  w d0d8e0 glow
  n 1a1010
end
""")
    def parts(body, head, ha=0.1, legs=None, tail=None, eye=1, belly=True):
        ps = []
        if tail:
            ps += chain(tail, [1.0, 0.9, 0.9], 'gfF', 0.2)
            ps.append(E(tail[-1][0], tail[-1][1], 1.0, 1.0, 0, 'ttt'))
        for (x0, y0, x1, y1) in (legs or [])[:2]:
            ps.append(C(x0, y0, x1, y1, 0.8, 0.7, 'gfF'))
        ps += chain(body, [2.0, 2.3, 2.2, 1.9][:len(body)], 'bad', 0.15)
        if belly:
            ps += [E(x, y + 1.2, 1.0, 0.8, 0, 'cCC') for (x, y) in body[1:]]
        hx, hy = head
        ps.append(E(hx, hy, 2.4, 1.9, ha, 'bad'))
        ps.append(C(hx + 1.0, hy + 0.5, hx + 3.6 * math.cos(ha), hy + 0.8 + 3.6 * math.sin(ha), 1.2, 0.6, 'bad'))
        ps.append(E(hx + 0.8, hy + 1.4, 1.4, 0.7, ha, 'cCC'))
        ps.append(E(hx - 1.2, hy - 1.8, 0.9, 0.8, 0, 'BAD'))
        for (x0, y0, x1, y1) in (legs or [])[2:]:
            ps.append(C(x0, y0, x1, y1, 0.9, 0.7, 'BAD'))
        if eye:
            ps += [P(round(hx + 0.6), round(hy - 0.5), 'e')]
        else:
            ps.append(P(round(hx + 0.6), round(hy - 0.5), 'd'))
        ps.append(P(round(hx + 3.9 * math.cos(ha)), round(hy + 0.8 + 3.9 * math.sin(ha)), 'n'))
        return ps
    G = 12.6
    std_body = [(7.0, 9.4), (10.0, 9.2), (13.0, 9.0), (15.6, 8.8)]
    std_legs = [(8.0, 10.4, 7.6, G), (15.2, 10.4, 15.6, G), (7.0, 10.4, 6.6, G), (14.2, 10.4, 14.6, G)]
    std_tail = [(5.4, 9.6), (3.0, 10.4), (1.0, 9.6)]
    frames = [
        ('stand', dict(body=std_body, head=(18.2, 8.0), legs=std_legs, tail=std_tail)),
        ('stand_blink', dict(body=std_body, head=(18.2, 8.0), legs=std_legs, tail=std_tail, eye=0)),
        ('sniff', dict(body=std_body, head=(18.4, 10.0), ha=0.6, legs=std_legs, tail=std_tail)),
        # "periscope": upright, looking around
        ('periscope', dict(body=[(11.0, 11.0), (11.6, 8.4), (12.2, 6.0), (12.8, 4.0)], head=(14.0, 2.4),
                           legs=[(11.0, 11.6, 10.4, G), (13.6, 6.0, 15.0, 6.6), (12.0, 11.6, 12.6, G), (13.0, 6.4, 14.6, 7.4)],
                           tail=[(9.4, 11.8), (6.4, 12.2), (3.6, 11.6)])),
        ('periscope2', dict(body=[(11.0, 11.0), (11.6, 8.4), (12.2, 6.0), (12.8, 4.0)], head=(14.2, 2.4), ha=-0.25,
                            legs=[(11.0, 11.6, 10.4, G), (13.6, 6.0, 15.0, 6.6), (12.0, 11.6, 12.6, G), (13.0, 6.4, 14.6, 7.4)],
                            tail=[(9.4, 11.8), (6.4, 12.2), (3.6, 11.6)])),
        # running in arcs: the back bends into a "U" and straightens
        ('run0', dict(body=[(6.4, 10.0), (9.4, 7.4), (12.6, 7.0), (15.4, 9.0)], head=(18.0, 8.6), ha=0.25,
                      legs=[(7.0, 11.0, 9.6, G), (15.0, 10.4, 13.0, G), (6.4, 11.0, 9.0, G), (15.6, 10.4, 13.6, G)],
                      tail=[(5.0, 10.6), (2.6, 10.0), (0.6, 8.6)])),
        ('run1', dict(body=[(5.0, 9.0), (8.4, 8.4), (12.0, 8.6), (15.8, 8.8)], head=(19.0, 7.8), ha=0.05,
                      legs=[(5.6, 10.0, 2.6, 11.6), (16.0, 9.8, 19.0, 11.6), (5.0, 10.0, 2.0, 11.0), (16.6, 9.8, 19.6, 11.0)],
                      tail=[(3.6, 9.0), (1.4, 8.0), (0.0, 6.4)])),
    ]
    for name, kw in frames:
        emit_frame(out, name, parts(**kw), 24, 14)
    out.append("""
clip idle loop fps=6 : stand*10 stand_blink stand*4 periscope*6 periscope2*6 periscope*3 stand*4 sniff*3
clip look loop fps=5 : periscope*6 periscope2*6
clip sniff loop fps=7 : sniff stand sniff sniff stand
clip run loop fps=12 speed=60 : run0 run1
clip move loop fps=10 speed=24 : run0 run1
creature
  move ground
  lane 0.4
  speed walk=24 run=60
  anim idle=idle move=move run=run look=look alert=look sniff=sniff eat=sniff
  intents wander=5 look=4 forage=2 hide=3 leave=2
  homes log brushwood bush
  fear 0.4
  curiosity 0.9
  size small
  scares mouse frog
  point head 6 -5
end
end
""")


def dormouse(out):
    """Dormouse: a nocturnal tree rodent — squirrel skeleton, grey fur, big eyes."""
    import io
    tmp = []
    squirrel(tmp)
    txt = '\n'.join(tmp)
    head_end = txt.index('end\n') + 4
    body = txt[head_end:]
    body = body[:body.index('\ncreature')]
    out.append("""# dormouse — generated by tools/sketch_rig.py (squirrel skeleton, different colours)
sprite dormouse
tags creature ground climber small mammal
anchor 11 21
palette
  b 8a8680 ~5
  a 6a6662 ~4
  d 44423e ~3
  B 8a8680 ~5 z+1
  A 6a6662 ~4 z+1
  D 44423e ~3 z+1
  g 54524e ~3 z-2
  f 3e3c3a ~3 z-2
  F 2a2826 ~2 z-2
  u 9a9690 ~8 z-1
  v 7a7670 ~7 z-1
  x 524e4a ~5 z-1
  c c4bcb0 ~3
  C a49c90 ~3
  k 7a5a3a z+2
  e 040404
  w e0e8f0 glow
  n 3a2e2c
end
""")
    out.append(body)
    out.append("""
creature
  move ground climb
  lane 0.2
  speed walk=16 run=40 climb=24
  anim idle=idle move=move run=run look=look eat=eat climb=climb climb_down=climb_down cling=cling alert=alert groom=flick
  intents forage=4 wander=2 climb=6 look=3 hide=3 leave=2
  homes hollow bush
  fear 0.6
  curiosity 0.6
  size small
  point head 4 -12
end
end
""")


def mole(out):
    out.append("""# mole — pokes out of the ground, sniffs, vanishes
sprite mole
tags creature ground tiny mammal burrow
anchor 8 10
palette
  m 3a2c22 ~6
  l 2a2018 ~4
  L 1c1610 ~3
  b 3a3a40 ~4 z+1
  a 26262c ~3 z+1
  d 16161a ~2 z+1
  p b07a74 z+2
  q 8a5a56 z+2
  P c08e86 z+2
end
""")
    def parts(stage, sniff=0.0, paw=0.0):
        ps = [E(8.0, 9.6, 6.4, 2.6 + min(stage, 2) * 0.4, 0, 'mlL', 0.3)]
        if stage >= 1:
            hy = 8.6 - stage * 1.4
            if stage >= 2:
                ps.append(E(8.0, hy + 1.4, 3.6, 2.4, 0, 'bad', 0.2))
                ps.append(E(4.6 - paw, hy + 2.2, 1.4, 1.0, 0.3, 'PPq'))
                ps.append(E(11.4 + paw, hy + 2.2, 1.4, 1.0, -0.3, 'PPq'))
            ps.append(C(8.0, hy, 8.6 + sniff, hy - 1.6 - sniff * 0.5, 1.0, 0.6, 'ppq'))
            ps.append(E(8.0, hy + 0.6, 2.4, 1.6, 0, 'bad'))
            ps.append(E(8.0, 10.0, 6.4, 1.2, 0, 'mlL', 0.4))
        return ps
    for name, kw in (('mound', dict(stage=0)), ('emerge1', dict(stage=1)), ('emerge2', dict(stage=2)),
                     ('sniff', dict(stage=2, sniff=0.8)), ('dig', dict(stage=2, paw=1.0)),
                     ('dig2', dict(stage=2, paw=-0.4))):
        emit_frame(out, name, parts(**kw), 17, 11)
    out.append("""
clip idle loop fps=5 : emerge2*6 sniff emerge2 sniff emerge2*4 dig dig2 dig dig2 emerge2*3
clip appear once fps=5 : mound mound emerge1 emerge2
clip vanish once fps=5 : emerge2 emerge1 mound mound
clip sniff loop fps=7 : sniff emerge2 sniff sniff emerge2
clip dig loop fps=9 : dig dig2
creature
  move ground
  lane 0.95
  burrow 1
  speed walk=0.1 run=0.1
  anim idle=idle appear=appear vanish=vanish look=sniff sniff=sniff eat=dig move=dig
  intents sniff=4 look=3 vanish=3
  fear 0.7
  size tiny
  point head 0 -4
end
end
""")


def salamander(out):
    out.append("""# fire salamander — black with yellow spots, slow
sprite salamander
tags creature ground tiny amphibian slow
anchor 10 7
palette
  b 3a3634 ~3
  a 22201e ~2
  d 121010
  B 3a3634 z+1
  A 22201e z+1
  D 121010 z+1
  g 1a1816 z-2
  f 121010 z-2
  F 0a0808 z-2
  y d0a020 ~8
  Y a07818 ~5
  e 040404
  w b0b8c0 glow
end
""")
    def parts(phase, head_up=0.0):
        ps = []
        pts = []
        for i in range(7):
            x = 1.0 + i * 2.4
            y = 5.0 + math.sin(phase + i * 0.9) * 0.8 * (1.0 - i / 8.0)
            pts.append((x, y))
        radii = [0.5, 0.8, 1.1, 1.5, 1.7, 1.6, 1.4]
        legs = []
        for (lx, ly), off in ((pts[3], 0.0), (pts[5], math.pi)):
            s_ = math.sin(phase + off) * 1.4
            legs.append((lx, ly, lx + s_, 7.0))
            legs.append((lx, ly, lx - s_ + 0.6, 7.0))
        ps += [C(x0, y0, x1, y1, 0.6, 0.5, 'gfF') for (x0, y0, x1, y1) in legs[1::2]]
        ps += chain(pts, radii, 'bad')
        hx, hy = pts[-1][0] + 2.4, pts[-1][1] - head_up
        ps.append(E(hx, hy, 2.2, 1.5, -head_up * 0.3, 'bad'))
        ps += [C(x0, y0, x1, y1, 0.6, 0.5, 'BAD') for (x0, y0, x1, y1) in legs[0::2]]
        for i in (1, 3, 4, 5):
            ps.append(P(round(pts[i][0]), round(pts[i][1] - 0.6), 'y'))
        ps += [P(round(pts[2][0]) + 1, round(pts[2][1]), 'Y'), P(round(hx - 1), round(hy - 1), 'y'),
               P(round(hx + 0.6), round(hy - 0.6), 'e')]
        return ps
    for i in range(4):
        emit_frame(out, 'walk%d' % i, parts(i * math.pi / 2), 22, 8)
    emit_frame(out, 'look', parts(0.0, 1.0), 22, 8)
    out.append("""
clip idle loop fps=2 : walk0*8 look*4
clip walk loop fps=4 speed=3 : walk0 walk1 walk2 walk3
clip look loop fps=2 : look*6 walk0*2
creature
  move ground
  lane 0.7
  speed walk=3 run=5
  anim idle=idle move=walk run=walk look=look
  intents wander=6 look=2 rest=2 leave=2 hide=1
  homes log brushwood
  fear 0.1
  curiosity 0.2
  size tiny
  point head 6 -2
end
end
""")


def slug(out):
    out.append("""# slug (no shell)
sprite slug
tags creature ground tiny slow
anchor 7 4
palette
  m a05a2a ~6
  M 7a4020 ~4
  K 4a2614 ~2
  s c88a52
  o 1a100a
end
""")
    def parts(st):
        return [C(1.0 - st, 3.0, 11.0 + st, 3.0, 0.8, 1.3, 'mMK'),
                E(8.0 + st * 0.5, 2.2, 2.8, 1.4, 0, 'mMK'),
                C(11.6 + st, 2.4, 12.6 + st, 0.4, 0.35, 0.3, 'MKK'),
                P(round(12.8 + st), 0, 'o'), P(round(7 + st * 0.5), 2, 's')]
    emit_frame(out, 'crawl0', parts(0.0), 15, 5)
    emit_frame(out, 'crawl1', parts(1.0), 15, 5)
    out.append("""
clip idle loop fps=1 : crawl0
clip crawl loop fps=2 speed=1.2 : crawl0 crawl1
creature
  move ground
  lane 0.8
  speed walk=1.2 run=1.2
  anim idle=idle move=crawl run=crawl
  intents wander=6 rest=2 leave=1
  fear 0.1
  size tiny
end
end
""")


def stag_beetle(out):
    out.append("""# stag beetle
sprite stag_beetle
tags creature ground tiny insect
anchor 7 7
palette
  b 6a3a26 ~5 z+1
  a 4a2618 ~4 z+1
  d 2a140c ~2 z+1
  h 2a2220 z+1
  l 1a1412
  m 8a4a2a z+2
  M 5a2e1a z+2
  w d0a080 glow
end
""")
    def parts(step, threat=0.0):
        ps = []
        for i, x in enumerate((4, 6, 8)):
            dx = step if i % 2 == 0 else -step
            ps += [P(x + dx, 7, 'l'), P(x + dx - 1, 6, 'l')]
        ps += [E(5.6, 4.6, 3.6, 2.2, 0, 'bad'), E(10.0, 4.4 - threat * 0.6, 1.6, 1.4, 0, 'hhh'),
               C(10.8, 3.8 - threat, 13.4, 2.0 - threat * 2.0, 0.6, 0.4, 'mMM'),
               C(10.8, 4.6 - threat, 13.6, 4.0 - threat * 2.4, 0.5, 0.4, 'MMM'),
               P(4, 3, 'w')]
        return ps
    emit_frame(out, 'crawl0', parts(0), 15, 8)
    emit_frame(out, 'crawl1', parts(1), 15, 8)
    emit_frame(out, 'threat', parts(0, 1.0), 15, 8)
    out.append("""
clip idle loop fps=1 : crawl0*6 threat*3
clip crawl loop fps=7 speed=4 : crawl0 crawl1
clip threat loop fps=2 : threat*4 crawl0
creature
  move ground
  lane 0.7
  speed walk=4 run=7
  anim idle=idle move=crawl run=crawl alert=threat look=threat
  intents wander=7 look=2 leave=1
  fear 0.05
  size tiny
end
end
""")


def spider(out):
    """Spider on a thread — decoration: descends, hangs, pulls itself back up (an object under a branch)."""
    out.append("""# spider on a thread (an object, not a creature with intents)
sprite spider
tags detail hanging
anchor 4 0
palette
  t 9aaab0 a=90 glow
  b 3a3430 z+1
  a 241e1a z+1
  d 141010 z+1
  l 1a1614 z+1
  e c08040 glow
end
""")
    def parts(length, twitch=0):
        ps = [C(4.0, 0.0, 4.0, length - 1.0, 0.3, 0.3, 'ttt')]
        by = length + 1.2
        for side in (-1, 1):
            for k in range(4):
                y0 = by - 0.6 + k * 0.5
                ex = 4 + side * (2.6 + (k % 2) * 0.6 + (twitch if k == 1 else 0))
                ey = y0 + (k - 1.5) * 1.1
                ps.append(C(4 + side * 0.8, y0, ex, ey, 0.3, 0.3, 'lll'))
        ps += [E(4.0, by + 1.4, 1.6, 1.8, 0, 'bad'), E(4.0, by - 0.6, 1.1, 0.9, 0, 'bad'), P(4, round(by - 1), 'e')]
        return ps
    lens = [2, 5, 8, 11, 14, 17]
    for i, L in enumerate(lens):
        emit_frame(out, 'drop%d' % i, parts(L), 9, 24)
    emit_frame(out, 'twitch', parts(17, 1), 9, 24)
    out.append("""
clip idle loop fps=3 : drop0*12 drop1 drop2 drop3 drop4 drop5*10 twitch drop5*8 twitch drop5*4 drop4 drop3 drop2 drop1
clip drop once fps=4 : drop0 drop1 drop2 drop3 drop4 drop5
clip hang loop fps=2 : drop5*6 twitch
end
""")


# ==========================================================================
# DEER — a full animal with the shared quadruped skeleton
# ==========================================================================

def deer(out):
    DY = 16.0                      # room above the head for the antlers
    G = 57.4 + DY

    def rot(px, py, cx, cy, ang):
        c_, s_ = math.cos(ang), math.sin(ang)
        dx, dy = px - cx, py - cy
        return cx + dx * c_ - dy * s_, cy + dx * s_ + dy * c_

    def antlers(hx, hy, ha, tx, ty):
        ps = []
        da = ha - 0.35              # the antlers rotate together with the head
        for keys, dx in (('xXX', 1.6), ('yYY', -0.8)):
            bx, by = hx - 1.0 + dx, hy - 2.8
            segs = [((bx, by), (bx - 4.0, by - 9.0), 1.1, 0.8),          # main beam
                    ((bx - 4.0, by - 9.0), (bx - 1.6, by - 19.0), 0.8, 0.5),
                    ((bx - 1.0, by - 3.0), (bx + 3.4, by - 6.6), 0.7, 0.4),   # brow tine
                    ((bx - 3.6, by - 9.8), (bx + 1.0, by - 13.4), 0.6, 0.4),  # bez tine
                    ((bx - 2.4, by - 15.4), (bx + 2.0, by - 19.4), 0.55, 0.35),  # crown
                    ((bx - 1.6, by - 19.0), (bx - 4.4, by - 21.4), 0.5, 0.3)]
            for (p0, p1, r0, r1) in segs:
                x0, y0 = rot(p0[0], p0[1], hx, hy, da)
                x1, y1 = rot(p1[0], p1[1], hx, hy, da)
                ps.append(C(x0, y0, x1, y1, r0, r1, keys))
        return ps

    def Y(*v):
        return tuple(x + DY if i % 2 else x for i, x in enumerate(v))

    rump = [E(11.8, 23.4 + DY, 2.6, 4.4, 0.0, 'cCa', 0.2), E(11.2, 19.6 + DY, 1.4, 2.2, 0.0, 'ddd')]
    c = dict(G=G, body=[(30.0, 26.0 + DY, 14.6, 8.4, -0.03), (18.6, 25.4 + DY, 7.8, 8.6, 0.0), (39.4, 25.0 + DY, 7.2, 8.8, 0.0)],
             chest=(44.0, 29.4 + DY, 2.8, 5.0, 0.2), bodyextra=rump,
             neck=(41.0, 22.0 + DY, 47.0, 10.0 + DY, 5.2, 3.4),
             hx=50.4, hy=8.0 + DY, hang=0.35, hrx=4.4, hry=3.4, snout=(4.2, 2.4, 1.4),
             ear=(-2.2, -2.4, 4.0, 1.6), headextra=antlers,
             hips=dict(nf=(41.0, 31.0 + DY), ff=(43.0, 31.0 + DY), nh=(20.0, 31.0 + DY), fh=(17.6, 31.0 + DY)),
             legr=(2.6, 1.0), eyeshine=True, fuzz=0.08)
    header = """# deer (full animal) — generated by tools/sketch_rig.py (shared quadruped skeleton)
sprite deer
tags creature ground large mammal rare
anchor 31 73
palette
  b 8a6a4c ~5
  a 6a4e36 ~4
  d 42301f ~3
  B 8a6a4c ~5 z+1
  A 6a4e36 ~4 z+1
  D 42301f ~3 z+1
  g 56402c z-2
  f 44321f z-2
  F 2e2216 z-2
  c c8bca8 ~3
  C a89a86 ~3
  k 3e3024 ~2 z+1
  K 241a12 z+1
  j 30241a z-2
  J 1c140e z-2
  x 6e6250 z-3
  X 54483a z-3
  y b0a48a ~4 z+2
  Y 887c66 ~3 z+2
  E b8e070 glow
  e 060604
  w c8d0d8 glow
  n 1a1210
end
"""
    graze = dict(neck=Y(41.0, 23.0, 46.6, 42.0) + (5.0, 3.0), hx=49.4, hy=46.8 + DY, hang=1.2)
    look = dict(neck=Y(41.0, 21.0, 45.6, 7.6) + (5.2, 3.4), hx=48.6, hy=5.4 + DY, hang=0.05)
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('ears', dict(ear=(-2.6, -1.8, 4.0, 1.6))),
        ('look', look),
        ('look_blink', dict(look, eye=0)),
        ('alert', dict(neck=Y(41.0, 20.0, 44.8, 5.6) + (5.2, 3.4), hx=47.6, hy=3.6 + DY, hang=-0.1,
                       ear=(-1.8, -2.8, 4.4, 1.6))),
        ('graze', graze),
        ('graze2', dict(graze, hy=47.8 + DY, hx=49.8, hang=1.3)),
        ('graze_blink', dict(graze, eye=0)),
        ('graze_up', dict(neck=Y(41.0, 22.4, 47.4, 30.0) + (5.0, 3.0), hx=50.6, hy=33.0 + DY, hang=0.8)),
    ]
    clips = """
clip idle loop fps=4 : stand*14 stand_blink stand*8 ears ears stand*6 look*6 look_blink look*4 stand*4
clip look loop fps=4 : look*8 look_blink look*6 stand*2
clip alert loop fps=4 : alert*12 look_blink alert*4
clip graze loop fps=5 : graze graze2 graze graze2 graze graze2 graze_blink graze graze2 graze graze2 graze*4 graze_up graze_up graze*2
clip walk loop fps=7 speed=13 : walk0 walk1 walk2 walk3 walk4 walk5
clip run loop fps=13 speed=48 : walk0 walk1 walk2 walk3 walk4 walk5
"""
    profile = """creature
  move ground
  lane -0.1
  speed walk=13 run=48
  anim idle=idle move=walk run=run look=look alert=alert eat=graze sniff=graze
  intents forage=7 look=3 wander=2 leave=3
  homes edge
  fear 0.5
  curiosity 0.3
  size large
  rare 1
  point head 20 -65
end
end
"""
    emit_quad(out, header, c, poses, 6, 64, 74, clips, profile, 0.3, 2.2, 0.6)


# ==========================================================================
# DHOLE (Asiatic wild dog) — a wild dog from the shared skeleton, lives in a pack
# ==========================================================================

def dhole(out):
    """Dhole — the cute version: a big round head, a short snout, big rounded ears
    with a light inside, big dark eyes with a highlight, a round body, shorter legs, a bushy tail."""
    G = 23.4

    def face(hx, hy, ha, tx, ty):
        ca, sa = math.cos(ha), math.sin(ha)
        return [E(hx + 1.6 * ca, hy + 1.9 + 1.6 * sa, 2.2, 1.3, ha, 'cCC'),      # light muzzle and cheeks
                E(hx - 0.6, hy + 2.0, 1.6, 1.0, 0.0, 'cCC')]

    c = dict(G=G, body=[(17.6, 14.2, 7.2, 4.8, 0.0), (12.2, 14.4, 4.8, 4.8, 0.0)],
             chest=(23.0, 15.2, 3.2, 3.9, 0.2),
             bodyextra=[E(17.4, 16.8, 5.8, 1.4, 0.0, 'cCC', 0.3)],
             neck=(22.4, 12.6, 24.6, 10.8, 3.4, 3.0),
             hx=27.0, hy=8.6, hang=0.1, hrx=5.1, hry=4.4, snout=(0.8, 2.2, 1.8),
             ear=(-1.8, -3.0, 3.6, 2.1), eartip=0.9, earlean=-0.6, earinner='cCC',
             headextra=face, bigeye=True, brow='c',
             hips=dict(nf=(23.2, 17.8), ff=(24.6, 17.8), nh=(12.8, 18.0), fh=(11.2, 18.0)),
             legr=(1.6, 1.2), tail=[(7.8, 12.8), (5.0, 13.8), (3.2, 16.2), (2.8, 19.0)],
             trad=[2.1, 3.0, 3.2, 2.8], tailkeys='tTT', tailfuzz=0.4, fuzz=0.05)
    header = """# dhole (Asiatic wild dog) — the cute version, generated by tools/sketch_rig.py
sprite dhole
tags creature ground medium mammal predator pack
anchor 18 23
palette
  b b86438 ~5
  a 944c2a ~5
  d 5e2e14 ~3
  B b86438 ~5 z+1
  A 944c2a ~5 z+1
  D 5e2e14 ~3 z+1
  g 70381a ~3 z-2
  f 562a10 ~3 z-2
  F 381a08 z-2
  c e0ccaa ~3
  C b8a284 ~3
  t 3e2c22 ~5 z-1
  T 241812 ~3 z-1
  k 7a4022 ~2 z+1
  K 4a2814 z+1
  j 52301a z-2
  J 321c0e z-2
  e 140c08
  w f0f4f8 glow
  n 1a100c
  r c8605a
end
"""
    sit = dict(body=[(15.0, 16.0, 5.0, 5.8, -0.75), (12.4, 19.4, 5.4, 3.6, 0.0)],
               chest=(18.4, 14.6, 2.6, 4.0, -0.3), bodyextra=[E(17.6, 17.8, 1.6, 3.0, -0.3, 'cCC', 0.2)],
               neck=(18.0, 12.6, 20.4, 9.2, 3.2, 2.8), hx=22.2, hy=7.6, hang=0.05,
               legs=dict(nf=(19.6, 14.0, 20.2, G), ff=(21.0, 14.0, 21.6, G), nh=None, fh=None),
               tail=[(8.4, 20.4), (12.2, 21.8), (16.4, 22.2), (20.4, 21.8)], trad=[2.2, 2.4, 2.3, 1.9])
    tilt = dict(hang=-0.35, hy=8.0, ear=(-1.4, -3.4, 3.8, 2.1), earlean=-1.4)
    bow = dict(body=[(17.0, 14.2, 7.4, 4.2, 0.32), (11.6, 11.8, 4.6, 4.6, 0.0)],
               chest=(23.4, 17.6, 2.6, 2.8, 0.3), bodyextra=[E(17.0, 17.4, 5.4, 1.2, 0.32, 'cCC', 0.3)],
               neck=(22.6, 16.2, 25.6, 16.0, 3.2, 2.8), hx=27.8, hy=14.6, hang=0.0,
               legs=dict(nf=(23.6, 18.6, 28.0, G), ff=(24.8, 18.6, 29.4, G), nh=(12.6, 14.8, 12.0, G), fh=(11.0, 14.8, 10.2, G)),
               tail=[(7.4, 9.6), (5.4, 7.2), (4.6, 4.4), (5.2, 2.0)], trad=[2.0, 2.8, 3.0, 2.6])
    poses = [
        ('stand', {}),
        ('stand_blink', dict(eye=0)),
        ('tilt', tilt),
        ('tilt_blink', dict(tilt, eye=0)),
        ('look', dict(hx=27.0, hy=7.4, hang=-0.15, neck=(22.4, 12.0, 25.2, 8.8, 3.4, 2.8))),
        ('alert', dict(hx=26.8, hy=6.4, hang=0.0, neck=(22.4, 11.6, 25.0, 7.8, 3.4, 2.8), ear=(-1.4, -2.8, 3.8, 2.0))),
        ('sniff', dict(hx=28.0, hy=16.8, hang=0.9, neck=(22.4, 13.2, 26.2, 15.4, 3.2, 2.6))),
        ('sniff2', dict(hx=28.4, hy=17.6, hang=1.0, neck=(22.4, 13.2, 26.4, 16.0, 3.2, 2.6))),
        ('sit', sit),
        ('sit_blink', dict(sit, eye=0)),
        ('sit_tilt', dict(sit, hang=-0.35, earlean=-1.4)),
        ('bow', bow),
        ('bow2', dict(bow, tail=[(7.4, 9.6), (6.2, 7.0), (6.8, 4.4), (8.4, 2.4)], hy=14.0)),
    ]

    def shifted(dy, **kw):
        """The standing pose with the body moved by dy (gallop bob) + changes."""
        d = dict(body=[(x, y + dy, rx, ry, a) for (x, y, rx, ry, a) in c['body']],
                 chest=(23.0, 15.2 + dy, 3.2, 3.9, 0.2),
                 bodyextra=[E(17.4, 16.8 + dy, 5.8, 1.4, 0.0, 'cCC', 0.3)],
                 neck=(22.4, 12.6 + dy, 24.6, 10.8 + dy, 3.4, 3.0), hy=8.6 + dy)
        d.update(kw)
        return d

    def mouth(hx, hy, open_=1):
        # open mouth below the snout tip (yawn, pant, howl)
        return [P(round(hx + 5.0), round(hy + 2.6), 'n'), P(round(hx + 6.0), round(hy + 2.6), 'n'),
                P(round(hx + 5.0), round(hy + 3.4), 'r' if open_ else 'n')]

    # tail wag (standing)
    wag1 = [(7.8, 12.8), (5.2, 11.2), (4.2, 8.6), (5.0, 6.0)]
    wag2 = [(7.8, 12.8), (5.0, 12.0), (2.8, 10.6), (1.4, 8.4)]
    # lying down: body on the ground, front legs stretched forward, tail curled around
    lie = dict(body=[(17.0, 19.4, 7.6, 3.6, 0.0), (11.6, 19.6, 4.8, 3.6, 0.0)],
               chest=(22.6, 19.6, 3.0, 3.2, 0.1), bodyextra=[E(17.0, 21.6, 6.0, 1.0, 0.0, 'cCC', 0.3)],
               neck=(22.0, 18.0, 24.4, 16.6, 3.2, 3.0), hx=26.8, hy=15.0, hang=0.12,
               legs=dict(nf=(23.0, 21.6, 30.0, G), ff=(24.2, 21.2, 31.2, G - 0.8), nh=(13.6, 21.4, 17.0, G),
                         fh=None),
               tail=[(7.0, 19.8), (6.0, 22.4), (9.6, 23.4), (13.6, 23.2)], trad=[2.2, 2.4, 2.2, 1.8])
    sleep = dict(lie, hx=27.6, hy=18.0, hang=0.25, eye=0, neck=(22.0, 18.4, 25.0, 18.4, 3.2, 3.0),
                 ear=(-1.6, -2.6, 3.2, 2.0), earlean=-1.6)
    # sitting scratch: a hind leg raised to the neck, head tilted towards it
    scratch = dict(sit, hang=0.5, hx=22.6, hy=8.4, eye=0,
                   legs=dict(nf=(19.6, 14.0, 20.2, G), ff=(21.0, 14.0, 21.6, G), nh=(13.6, 19.4, 18.4, 13.2), fh=None))
    scratch2 = dict(scratch, legs=dict(nf=(19.6, 14.0, 20.2, G), ff=(21.0, 14.0, 21.6, G),
                                       nh=(13.6, 19.4, 17.4, 14.6), fh=None))
    # whistle / howl: dholes call with the head up
    howl = dict(sit, hang=-0.85, hx=21.0, hy=6.0, neck=(18.0, 12.4, 19.8, 7.6, 3.2, 2.8), eye=0,
                ear=(-1.8, -2.4, 3.2, 2.0), earlean=-1.8)
    poses += [
        ('wag1', dict(tail=wag1)),
        ('wag2', dict(tail=wag2)),
        ('wag_blink', dict(tail=wag1, eye=0)),
        ('pant', dict(bodyextra=c['bodyextra'] + mouth(27.0, 8.6))),
        ('lie', lie),
        ('lie_blink', dict(lie, eye=0)),
        ('lie_look', dict(lie, hang=-0.2, hy=14.4)),
        ('sleep', sleep),
        ('sleep2', dict(sleep, body=[(17.0, 19.2, 7.6, 3.8, 0.0), (11.6, 19.6, 4.8, 3.6, 0.0)])),
        ('yawn', dict(lie, hang=-0.45, hy=14.4, eye=0, bodyextra=lie['bodyextra'] + mouth(25.6, 12.4))),
        ('scratch', scratch),
        ('scratch2', scratch2),
        ('howl', dict(howl, bodyextra=sit['bodyextra'] + [P(25, 3, 'n'), P(25, 4, 'n')])),
        ('shake1', shifted(-0.4, body=[(17.6, 13.8, 7.6, 5.0, 0.12), (12.2, 14.2, 5.2, 5.0, 0.0)],
                           ear=(-1.0, -3.2, 3.6, 2.4), earlean=0.6, hang=0.35, eye=0, fuzz=0.5,
                           tail=[(7.8, 12.8), (5.4, 11.0), (4.4, 8.2), (5.6, 5.6)])),
        ('shake2', shifted(-0.4, body=[(17.6, 13.8, 7.6, 5.0, -0.12), (12.2, 14.2, 5.2, 5.0, 0.0)],
                           ear=(-2.4, -2.6, 3.6, 2.4), earlean=-1.8, hang=-0.25, eye=0, fuzz=0.5,
                           tail=[(7.8, 12.8), (4.8, 13.2), (2.4, 12.0), (1.0, 9.6)])),
        ('eat0', dict(hx=28.2, hy=18.0, hang=1.0, neck=(22.4, 13.2, 26.4, 16.2, 3.2, 2.6))),
        ('eat1', dict(hx=28.0, hy=17.2, hang=0.8, neck=(22.4, 13.2, 26.2, 15.6, 3.2, 2.6), eye=0)),
    ]
    # gallop: extended (airborne) -> front landing -> gathered -> hind push
    tailrun = [(7.8, 12.6), (4.8, 12.0), (2.2, 12.4), (0.6, 13.4)]
    gal = [
        ('gal0', shifted(-1.2, legs=dict(nf=(23.2, 16.6, 30.0, 20.0), ff=(24.6, 16.6, 31.0, 21.4),
                                         nh=(12.8, 16.8, 6.0, 21.0), fh=(11.2, 16.8, 5.0, 20.0)),
                         hx=28.0, hang=0.15, tail=[(x, y - 1.2) for x, y in tailrun])),
        ('gal1', shifted(0.0, legs=dict(nf=(23.2, 17.8, 26.4, G), ff=(24.6, 17.8, 28.2, G - 1.6),
                                        nh=(12.8, 18.0, 8.0, 22.6), fh=(11.2, 18.0, 7.0, 21.0)),
                         hx=27.4, hy=9.4, hang=0.25, tail=tailrun)),
        ('gal2', shifted(0.6, body=[(17.0, 14.8, 6.6, 5.0, -0.06), (12.6, 14.6, 4.8, 4.8, 0.0)],
                         legs=dict(nf=(22.6, 18.4, 20.4, G), ff=(23.8, 18.4, 19.4, G - 1.4),
                                   nh=(13.2, 18.6, 17.4, G), fh=(11.6, 18.6, 16.2, G - 1.0)),
                         hx=26.6, hy=9.8, hang=0.3, tail=[(x, y + 0.6) for x, y in tailrun])),
        ('gal3', shifted(-0.4, legs=dict(nf=(23.2, 17.4, 26.6, 20.4), ff=(24.6, 17.4, 27.4, 21.6),
                                         nh=(12.8, 17.6, 14.0, G), fh=(11.2, 17.6, 12.2, G)),
                         hx=27.4, hang=0.1, tail=[(x, y - 0.4) for x, y in tailrun])),
    ]
    poses += gal
    # transitions: half crouch (stand <-> lie), half sit (stand <-> sit)
    crouch = shifted(2.6, legs=dict(nf=(23.2, 19.6, 26.2, G), ff=(24.6, 19.6, 27.4, G),
                                    nh=(12.8, 19.8, 15.0, G), fh=(11.2, 19.8, 13.6, G)),
                     hx=27.0, hy=11.8, hang=0.15, tail=[(7.6, 15.6), (5.0, 17.2), (4.0, 19.6), (4.8, 21.8)])
    sitdown = dict(body=[(15.8, 15.2, 6.0, 5.2, -0.45), (12.6, 18.2, 5.2, 4.2, 0.0)],
                   chest=(20.4, 14.8, 2.8, 3.8, -0.2), bodyextra=[E(17.6, 17.6, 3.0, 2.0, -0.3, 'cCC', 0.3)],
                   neck=(19.6, 12.6, 22.2, 9.6, 3.3, 2.9), hx=24.2, hy=7.8, hang=0.05,
                   legs=dict(nf=(21.0, 15.0, 21.8, G), ff=(22.4, 15.0, 23.2, G), nh=(13.6, 19.6, 16.0, G), fh=None),
                   tail=[(8.0, 18.8), (6.0, 20.6), (6.8, 22.6), (9.6, 23.0)], trad=[2.2, 2.6, 2.4, 2.0])
    # sniffing the air: head up, the nose twitches
    sniffair = dict(hx=27.4, hy=6.8, hang=-0.32, neck=(22.4, 11.8, 25.4, 8.0, 3.4, 2.8))
    # looking back over the shoulder
    lookback = dict(hx=20.4, hy=7.6, hang=math.pi - 0.25, neck=(21.6, 12.4, 20.8, 9.6, 3.4, 3.0),
                    ear=(1.0, -3.0, 3.6, 2.1), earlean=0.6)
    # stretches: front (bow, eyes closed) and back (hind legs stretched out behind)
    stretchb = shifted(0.8, body=[(18.4, 14.6, 7.4, 4.4, 0.06), (12.6, 15.2, 4.6, 4.4, 0.0)],
                       legs=dict(nf=(23.8, 18.4, 25.0, G), ff=(25.2, 18.4, 26.4, G),
                                 nh=(12.4, 18.2, 6.6, G), fh=(10.8, 18.2, 5.4, G - 0.6)),
                       hx=28.6, hy=9.6, hang=0.12, eye=0, tail=[(7.8, 13.6), (5.0, 14.0), (2.8, 15.2), (1.4, 16.8)])
    # pounce (playful, like a fox on a mouse): crouch -> leap -> nose down
    pcrouch = dict(bow, ear=(-1.2, -3.4, 3.8, 2.1), earlean=0.2, hy=14.0)
    pcrouch2 = dict(pcrouch, tail=[(7.4, 9.6), (6.6, 7.0), (7.6, 4.6), (9.4, 3.0)],
                    body=[(17.0, 14.0, 7.4, 4.2, 0.36), (11.6, 11.4, 4.6, 4.6, 0.0)])
    pleap = dict(body=[(17.6, 10.6, 7.2, 4.6, 0.3), (12.2, 8.8, 4.6, 4.6, 0.2)],
                 chest=(23.0, 13.0, 3.0, 3.6, 0.4), bodyextra=[E(17.4, 13.2, 5.6, 1.3, 0.3, 'cCC', 0.3)],
                 neck=(22.6, 11.2, 25.6, 11.6, 3.2, 2.8), hx=28.2, hy=11.8, hang=0.7,
                 legs=dict(nf=(23.2, 14.4, 27.2, 19.8), ff=(24.6, 14.4, 28.4, 20.6),
                           nh=(12.6, 11.6, 7.6, 15.6), fh=(11.0, 11.6, 6.4, 14.6)),
                 tail=[(7.6, 7.6), (5.2, 5.6), (4.4, 3.2), (5.0, 1.2)], trad=[2.0, 2.8, 3.0, 2.6],
                 ear=(-1.0, -3.0, 3.6, 2.2), earlean=0.4)
    pland = dict(bow, hx=28.6, hy=18.4, hang=1.05, neck=(22.6, 16.4, 26.2, 17.6, 3.2, 2.8))
    pland2 = dict(pland, tail=[(7.4, 9.6), (6.2, 7.0), (6.8, 4.4), (8.4, 2.4)], eye=0)
    poses += [
        ('crouch', crouch),
        ('sitdown', sitdown),
        ('sniffair1', sniffair),
        ('sniffair2', dict(sniffair, hang=-0.42, hy=6.4)),
        ('lookback', lookback),
        ('lookback_blink', dict(lookback, eye=0)),
        ('stretchf', dict(bow, eye=0, hy=15.0)),
        ('stretchb', stretchb),
        ('pcrouch', pcrouch),
        ('pcrouch2', pcrouch2),
        ('pleap', pleap),
        ('pland', pland),
        ('pland2', pland2),
    ]
    clips = """
clip idle loop fps=5 : stand*16 stand_blink stand*12 tilt*6 tilt_blink tilt*2 stand*10 stand_blink stand*8
clip wag loop fps=8 : wag1 wag2 wag1 wag2 wag1 wag2 stand*8 wag_blink wag2 wag1 wag2 stand*10 pant*12 stand*6
clip pant loop fps=6 : pant*4 stand pant*4 stand_blink pant*6 stand*4
clip sniffair loop fps=6 : stand*3 sniffair1 sniffair2 sniffair1 sniffair2 sniffair1*3 sniffair2 sniffair1 stand*6 stand_blink stand*4
clip lookback loop fps=4 : stand*3 lookback*8 lookback_blink lookback*4 stand*6
clip look loop fps=4 : look*8 stand*3 stand_blink look*6 stand*3
clip howl loop fps=3 : sit*3 howl*8 sit_blink sit*3 howl*6 sit*4
clip alert loop fps=4 : alert*10 stand_blink alert*4
clip sniff loop fps=7 : sniff sniff2 sniff sniff2 sniff*3 sniff2 stand
clip eat loop fps=6 : eat0 eat1 eat0 eat1 eat0*2 eat1 eat0 eat1 stand*2 look*3
clip sit loop fps=3 : sit*12 sit_blink sit*8 sit_tilt*5 sit*6 sit_blink sit*6
clip sit_in once fps=8 : sitdown sitdown
clip sit_out once fps=8 : sitdown sitdown
clip howl_in once fps=8 : sitdown sitdown
clip howl_out once fps=8 : sitdown sitdown
clip lie loop fps=3 : lie*12 lie_blink lie*8 lie_look*6 lie*6 yawn*3 lie*8
clip lie_in once fps=6 : crouch crouch crouch
clip lie_out once fps=6 : crouch crouch
clip sleep loop fps=2 : sleep*3 sleep2*3 sleep*3 sleep2*3 sleep*3 sleep2*3 sleep*3 sleep2*3 sleep*3 sleep2*3 sleep*3 sleep2*3 lie_blink sleep*3 sleep2*3 sleep*3 sleep2*3
clip sleep_in once fps=5 : crouch crouch lie lie_blink lie*2 yawn*3 lie lie_blink
clip sleep_out once fps=5 : lie_blink lie yawn*2 crouch crouch
clip scratch loop fps=10 : sit*3 scratch scratch2 scratch scratch2 scratch scratch2 scratch scratch2 scratch scratch2 sit*6 sit_blink sit*5
clip scratch_in once fps=8 : sitdown sitdown
clip scratch_out once fps=8 : sitdown sitdown
clip shake loop fps=12 : stand*4 shake1 shake2 shake1 shake2 shake1 shake2 stand*20 stand_blink stand*12
clip stretch loop fps=4 : stretchf*8 bow stand*3 stretchb*7 stand*3 stand_blink stand*4
clip play loop fps=6 : bow bow2 bow bow2 bow*3 stand wag1 wag2 wag1 wag2 bow bow2 bow*2 stand*3
clip pounce loop fps=8 : pcrouch*5 pcrouch2 pcrouch pcrouch2 pcrouch*3 pleap pleap pland*4 pland2 pland*3 stand*3 wag1 wag2 wag1 wag2 stand*4
clip walk loop fps=9 speed=15 : walk0 walk1 walk2 walk3 walk4 walk5
clip run loop fps=12 speed=60 : gal0 gal1 gal2 gal3
"""
    profile = """creature
  move ground
  lane 0.3
  speed walk=15 run=60
  anim idle=idle|wag|sniffair|lookback|idle|pant move=walk run=run look=look|howl|lookback|look alert=alert sniff=sniff eat=eat rest=sleep|lie|sleep|sit sit=sit groom=scratch|stretch|scratch|stretch|shake play=play|pounce|play
  intents wander=3 forage=2 sniff=3 look=3 rest=4 groom=2 chase=2.2 frolic=2 follow=2 leave=1
  rest_time 10 35
  chases rabbit squirrel mouse mole frog hedgehog dormouse
  homes edge
  fear 0.25
  curiosity 0.7
  size medium
  pack 4
  scares rabbit squirrel mouse mole hedgehog frog deer weasel dormouse
  point head 10 -15
end
end
"""
    emit_quad(out, header, c, poses, 6, 38, 24, clips, profile, 0.45, 1.6, 0.5)


CREATURES = {
    'rabbit': rabbit, 'squirrel': squirrel, 'fox': fox, 'owl': owl, 'hedgehog': hedgehog,
    'mouse': mouse, 'frog': frog, 'snail': snail, 'beetle': beetle, 'moth': moth, 'bat': bat,
    'eyes': eyes, 'spirit': spirit, 'deer': deer,
    'badger': badger, 'boar': boar, 'wildcat': wildcat, 'weasel': weasel, 'dormouse': dormouse,
    'dhole': dhole, 'mole': mole, 'salamander': salamander, 'slug': slug, 'stag_beetle': stag_beetle, 'spider': spider,
}


def main():
    if len(sys.argv) < 2 or sys.argv[1] == '--list':
        print('\n'.join(sorted(CREATURES)))
        return
    if sys.argv[1] == '--all':
        d = sys.argv[2] if len(sys.argv) > 2 else 'assets/creatures'
        for name, fn in CREATURES.items():
            out = []
            fn(out)
            with open(os.path.join(d, name + '.spr'), 'w') as fp:
                fp.write('\n'.join(out) + '\n')
            print('written:', name)
        return
    out = []
    CREATURES[sys.argv[1]](out)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
