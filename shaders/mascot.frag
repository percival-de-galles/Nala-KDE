#version 440

// Nala's body. Everything the mascot is made of -- silhouette, eyes and the
// notification badge -- is a signed distance field evaluated per pixel, so the
// shape stays crisp at any size and morphs between forms by blending distances
// rather than by interpolating vertices.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;

    vec4 bodyColor;
    vec4 eyeColor;
    vec4 badgeColor;

    // Silhouette: blend `formMix` of the way from `formA` to `formB`.
    float formA;
    float formB;
    float formMix;

    // Whole-body deformation.
    float squashX;
    float squashY;
    float bodyScale;
    float roll;

    // Eyes, in units of the body radius, relative to the body centre.
    vec2 eyeLeft;
    vec2 eyeRight;
    float eyeWidth;
    float eyeHeight;
    float eyeRound;

    // Extras.
    float badge;      // notification dot, 0..1
    float dotsSpread; // "..." separation, 0..1
    float dotsShrink; // how far the core has collapsed into the "..." run
    float dotsPhase;  // travelling emphasis around the "..." run
};

// Every constant below is a half-extent in the item's [-1,1] space, measured
// from the reference animation frame by frame. See docs/animation.md.
const float kRadius = 0.529; // idle body radius
const float kDotGap = 0.285; // "..." dot spacing
const float kDotSide = 0.092;
const float kDotCore = 0.105;

float sdCircle(vec2 p, float r) { return length(p) - r; }

float sdEllipse(vec2 p, vec2 r) {
    // Cheap bounded approximation: exact enough for a silhouette this smooth.
    float k = length(p / r);
    return (k - 1.0) * min(r.x, r.y);
}

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdSegment(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float sdHexagon(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

float sdTriangle(vec2 p, float r) {
    const float k = 1.7320508;
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0)
        p = vec2(p.x - k * p.y, -k * p.x - p.y) * 0.5;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

vec2 rotate(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// Smooth union -- lets separate lumps fuse into one another the way the real
// mascot's parts do when it reassembles.
float smoothUnion(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// The "..." run. At spread 0 with a full core this is exactly the idle circle,
// which is what makes the collapse into three dots one continuous motion.
float sdDots(vec2 p, float spread, float shrink) {
    float core = mix(kRadius, kDotCore, shrink);
    float side = kDotSide * spread;
    float gap = kDotGap * spread;
    float d = sdCircle(p, core);
    d = min(d, sdCircle(p - vec2(gap, 0.0), side));
    d = min(d, sdCircle(p + vec2(gap, 0.0), side));
    return d;
}

// In the reference the three dots pulse in turn, so the run reads as a
// progress indicator rather than as punctuation.
float dotsEmphasis(vec2 p) {
    float best = 1e9;
    float emphasis = 1.0;
    for (int i = 0; i < 3; ++i) {
        float d = length(p - vec2(float(i - 1) * kDotGap, 0.0));
        if (d < best) {
            best = d;
            float wave = cos(dotsPhase - float(i) * 2.0943951);
            emphasis = 0.34 + 0.66 * max(0.0, wave) * max(0.0, wave);
        }
    }
    return emphasis;
}

// Leans right by about 16 degrees, with a stem of nearly constant width and a
// dot a little narrower than the stem -- all measured off the reference.
float sdExclaim(vec2 p) {
    p = rotate(p, 0.27);
    float stem = sdSegment(p, vec2(0.0, 0.212), vec2(0.0, -0.045), 0.073);
    stem += 0.012 * smoothstep(0.18, -0.045, p.y); // barely tapered
    float point = sdCircle(p - vec2(0.0, -0.213), 0.057);
    return min(stem, point);
}

// Round at the top, drawn down to a point at the bottom. The reference is
// widest 40% of the way down, which is where the ball centre sits.
float sdTeardrop(vec2 p) {
    p = rotate(p, 0.07); // the tip drifts left, as the reference's does
    float ball = sdCircle(p - vec2(0.0, 0.060), 0.225);
    float tip = sdSegment(p, vec2(0.0, 0.060), vec2(0.0, -0.272), 0.026);
    return smoothUnion(ball, tip, 0.15);
}

// Wider at the bottom than the top, rather than a plain ellipse.
float sdEgg(vec2 p) {
    p.y += 0.025;
    float taper = 1.0 - 0.15 * clamp(p.y / 0.50, -1.0, 1.0);
    return sdEllipse(vec2(p.x / taper, p.y), vec2(0.436, 0.50)) * taper;
}

float form(vec2 p, float which) {
    int f = int(which + 0.5);
    if (f == 1) return sdEgg(p);
    // Pointy-top, and rounded hard: the reference hexagon is much softer than
    // a textbook one, and measures taller than it is wide.
    if (f == 2) return sdHexagon(rotate(p, 1.5707963), 0.33) - 0.16;
    if (f == 3) return sdTriangle(p, 0.35) - 0.175;
    if (f == 4) return sdExclaim(p);
    if (f == 5) return sdTeardrop(p);
    if (f == 6) return sdDots(p, dotsSpread, dotsShrink);
    if (f == 7) return sdCircle(p, 0.083);
    return sdCircle(p, kRadius);
}

float isDots(float which) { return int(which + 0.5) == 6 ? 1.0 : 0.0; }

// Which forms carry a face. In the reference the exclamation mark, the "..."
// run, the teardrop and the sleeping dot are all featureless, so the eyes fade
// out across the morph instead of riding along on a shape that has no face.
float faceWeight(float which) {
    int f = int(which + 0.5);
    return (f == 0 || f == 1 || f == 2 || f == 3) ? 1.0 : 0.0;
}

// A single eye: a rounded vertical slit that can flatten to a blink dash or
// open out into a near-circle.
float sdEye(vec2 p, vec2 centre) {
    vec2 extent = vec2(eyeWidth, eyeHeight) * kRadius;
    float r = min(extent.x, extent.y) * eyeRound;
    return sdRoundBox(p - centre * kRadius, extent, r);
}

void main() {
    vec2 p = (qt_TexCoord0 - 0.5) * 2.0;

    // Undo the body transform so the SDFs stay in their own tidy space.
    p /= max(bodyScale, 0.0001);
    p = rotate(p, -roll);
    p /= vec2(max(squashX, 0.0001), max(squashY, 0.0001));
    p.y = -p.y; // screen y grows downward; the shapes are authored y-up

    float a = form(p, formA);
    float b = form(p, formB);
    float body = mix(a, b, formMix);

    // One pixel, measured in the same space the distances live in.
    float px = fwidth(p.x) * 1.15;

    float bodyMask = clamp(0.5 - body / px, 0.0, 1.0);

    float eyes = min(sdEye(p, vec2(eyeLeft.x, -eyeLeft.y)),
                     sdEye(p, vec2(eyeRight.x, -eyeRight.y)));
    // Eyes only exist where there is body to carve them out of, and only on
    // the forms that have a face at all.
    float face = mix(faceWeight(formA), faceWeight(formB), formMix);
    float eyeMask = clamp(0.5 - eyes / px, 0.0, 1.0) * bodyMask * face;

    float badgeD = sdCircle(p - vec2(0.375, 0.375), 0.115 * badge);
    float badgeMask = badge > 0.001 ? clamp(0.5 - badgeD / px, 0.0, 1.0) : 0.0;

    // The badge sits on the rim and pokes outside the silhouette, so it
    // contributes coverage of its own rather than only recolouring the body.
    float alpha = max(bodyMask * bodyColor.a, badgeMask);

    // Fade the quiet dots rather than greying them: on a dark desktop a fixed
    // grey would read as a smudge, while lower opacity reads correctly on any
    // wallpaper.
    float dotsWeight = mix(isDots(formA), isDots(formB), formMix);
    if (dotsWeight > 0.001)
        alpha *= mix(1.0, dotsEmphasis(p), dotsWeight * dotsSpread);

    vec3 rgb = bodyColor.rgb;
    rgb = mix(rgb, eyeColor.rgb, eyeMask);
    rgb = mix(rgb, badgeColor.rgb, badgeMask);

    fragColor = vec4(rgb * alpha, alpha) * qt_Opacity;
}
