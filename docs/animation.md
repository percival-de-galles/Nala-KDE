# Where the numbers come from

Every proportion and timing in Nala was measured from the reference animation
(1458×1458, 60 fps, 1845 frames) rather than eyeballed. This note records what
was measured and how, so the constants in `shaders/mascot.frag` and
`src/mascot.cpp` can be checked or re-derived.

## Method

Frames were sampled with ffmpeg and measured with a small numpy script:

```bash
ffmpeg -i reference.mp4 -vf "select='not(mod(n\,3))',scale=180:180,tile=8x8" -frames:v 1 sheet.png
```

For a single frame, the body is the set of pixels darker than mid-grey; the
eyes are the *enclosed* light regions inside it, found by flood-filling the
background inward so the page around her is not mistaken for an eye.

Extents below are **half-extents in the shader's coordinate space**, where the
item spans `[-1, 1]`. A shape covering fraction `f` of the frame's width has a
half-extent of `f`.

## Palette

| Role | Value | Notes |
| --- | --- | --- |
| Body | `#0a090c` | Near-black with a faint violet cast |
| Eyes | `#fdfdfd` | |
| Notification badge | `#3ea1f5` | |
| Orbit arcs | `hsv(h, 0.55, 0.84)` | Hue swept around the wheel |

The arc colours were recovered by clustering every saturated pixel of a
"thinking" frame by hue: all sixteen populated bins landed at S ≈ 0.5–0.6 and
V ≈ 0.75–0.88, i.e. one rainbow at constant saturation and value.

## Silhouettes

Measured against Nala's rendered output (`scripts/poses.sh`):

| Form | Reference w × h | Nala w × h |
| --- | --- | --- |
| Circle | 0.529 × 0.536 | 0.528 × 0.528 |
| Egg | 0.436 × 0.525 | 0.440 × 0.499 |
| Hexagon | 0.483 × 0.530 | 0.490 × 0.541 |
| Triangle | 0.528 × 0.486 | 0.525 × 0.478 |
| Exclamation | 0.132 × 0.285 | 0.114 × 0.281 |
| Teardrop | 0.227 × 0.298 | 0.214 × 0.293 |
| Sleeping dot | 0.083 × 0.082 | 0.082 × 0.082 |

Four findings worth recording, because all of them are easy to get backwards:

- **The hexagon is pointy-top.** It measures taller than wide (0.530 vs 0.483),
  and the silhouette is only 0.043 wide four pixels below its apex — a vertex,
  not an edge.
- **The hexagon and triangle are rounded far harder than textbook shapes.**
  Fitting `half_height - half_width = (1.1547 - 1) · r` to the hexagon gives a
  circumradius of 0.30 against a corner radius of 0.18.
- **The teardrop points down.** It is round at the top and drawn to a point at
  the bottom, widest 40% of the way down. Building it the other way up is an
  easy mistake and scores badly against the reference (0.64 IoU against 0.89).
- **The exclamation mark leans right and barely tapers.** Its centre drifts
  from +0.111 at the apex to −0.169 at the dot, about 16°, and the stem holds a
  near-constant width (0.255 → 0.236) rather than narrowing to a point. Its dot
  is slightly narrower than the stem.

### Checking a change

Silhouettes are compared by intersection-over-union against the matching
reference frame, after normalising both to a common bounding box:

| Form | IoU |
| --- | --- |
| Circle | 0.925 |
| Egg | 0.902 |
| Hexagon | 0.953 |
| Triangle | 0.951 |
| Exclamation | 0.849 |
| Teardrop | 0.885 |
| Sleeping dot | 0.986 |
| **Mean** | **0.921** |

The residual is mostly edge antialiasing and the fact that the reference frames
are single moments of a continuously breathing body — the reference circle
measures 0.529 × 0.536, i.e. mid-bob, while Nala's is exactly round at rest.

The window is sized `1.89 ×` the body diameter (`1 / 0.529`) so forms that
reach past the idle circle — the exclamation mark, the orbit arcs — are not
clipped.

## Face

| Property | Value |
| --- | --- |
| Eye half-width | 0.145 R |
| Eye half-height | 0.25 R |
| Eye separation | 0.47 R |
| Rest position of the pair | (+0.19 R, −0.13 R), i.e. up and to the right |
| Slit aspect (h / w) | 1.71 |

Nala renders at a 0.476 R separation and a 1.73 slit aspect.

Only the circle, egg, hexagon and triangle carry a face. The exclamation mark,
the teardrop, the "..." run and the sleeping dot are featureless, so the eyes
fade out across the morph rather than riding along on a shape that has none.

## Timing

| Behaviour | Value |
| --- | --- |
| Blink | ~0.155 s, closing faster than it opens |
| Blink interval | 2.2–5.4 s |
| Form morph | 0.30–0.45 s |
| Idle flourish interval | 9–18 s |
| Sleep after | 75 s idle |
| "..." pulse | ~4.4 rad/s, one dot at a time |

## The "..." run

The three dots are not a separate shape. They are the circle with three
parameters animated — core radius, side radius and separation — so that at rest
the form *is* the idle circle. That is what makes the collapse read as one
continuous motion instead of a cross-fade between two drawings.

The quiet dots are faded rather than greyed. The reference draws them grey on a
near-white page; on a desktop overlay a fixed grey reads as a smudge over dark
wallpaper, while lower opacity reads correctly over anything.
