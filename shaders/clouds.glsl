// Volumetric cloud layer, shared by the cloud pass (clouds.frag) and the terrain (cloud
// shadows). Constants (kCloudBottom, kCloudExtinction, ...) are generated from the C++
// CloudParameters and prepended; the atmosphere prelude comes before this file. Units: km.

uniform samplerCube uCloudWeather; // cloudiness 0..1 over the planet (core/atmosphere/Clouds)
uniform sampler3D uCloudNoise;     // R: Perlin-Worley shape, GBA: Worley octaves (tiling)
uniform vec3 uCloudOrigin;  // eye position (atmosphere km) mod kCloudRegionalTile, in double on the CPU
uniform float uCloudCoverage;
uniform int uCloudsEnabled;

bool sphereHit(vec3 origin, vec3 dir, float radius, out vec2 t) {
    float b = dot(origin, dir);
    float disc = b * b - (dot(origin, origin) - radius * radius);
    if (disc < 0.0) {
        t = vec2(0.0);
        return false;
    }
    float s = sqrt(disc);
    t = vec2(-b - s, -b + s);
    return true;
}

// GLSL twin of rayShellIntervals() in Clouds.cpp (tested there): up to two intervals of
// origin + t dir inside the cloud shell, clipped to t >= 0 and to the ground.
int cloudIntervals(vec3 origin, vec3 dir, out vec2 first, out vec2 second) {
    first = vec2(0.0);
    second = vec2(0.0);
    vec2 o;
    if (!sphereHit(origin, dir, kBottomRadius + kCloudTop, o) || o.y <= 0.0) {
        return 0;
    }
    float stop = o.y;
    vec2 g;
    if (sphereHit(origin, dir, kBottomRadius, g) && g.x > 0.0) {
        stop = min(stop, g.x);
    }
    vec2 candidates[2];
    vec2 i;
    if (!sphereHit(origin, dir, kBottomRadius + kCloudBottom, i)) {
        candidates[0] = vec2(o.x, min(o.y, stop)); // passes above the cloud base: one chord
        candidates[1] = vec2(0.0);
    } else {
        candidates[0] = vec2(o.x, min(i.x, stop));
        candidates[1] = vec2(i.y, min(o.y, stop));
    }
    int count = 0;
    for (int k = 0; k < 2; ++k) {
        vec2 c = vec2(max(candidates[k].x, 0.0), candidates[k].y);
        if (c.y > c.x) {
            if (count == 0) {
                first = c;
            } else {
                second = c;
            }
            ++count;
        }
    }
    return count;
}

float remap(float v, float l0, float h0, float l1, float h1) {
    return l1 + (v - l0) * (h1 - l1) / (h0 - l0);
}

// Cloud density 0..1 at a point of the atmosphere model `p` (km from the planet centre). `q` is
// the same point in world-anchored noise coordinates (eye-relative km + uCloudOrigin), so the
// shapes stay fixed in the world. `detail` = 0 skips the erosion (light rays, far away).
// `footprint` = size of a pixel there (km): once the shapes shrink below a pixel, thresholding
// their mip-filtered AVERAGE would turn every cloudy region into one solid blob, so the density
// fades to its expected value instead (what a pixel full of small clouds looks like on average).
float cloudDensity(vec3 p, vec3 q, float detail, float footprint) {
    float r = length(p);
    float h = (r - kBottomRadius - kCloudBottom) / (kCloudTop - kCloudBottom);
    if (h <= 0.0 || h >= 1.0) {
        return 0.0;
    }
    // Where: the planet's weather, thresholded by the coverage setting. Thin cover = small,
    // flat cumulus; thick cover = towering ones (the top of the profile rises with it).
    // Regional patterns (a coarser look-up of the same noise) break up the repetition of the
    // 12 km shapes, and add structure below the weather map's ~40 km texels.
    float weather = texture(uCloudWeather, p / r).r;
    float regional = texture(uCloudNoise, q / kCloudRegionalTile).r;
    weather += (regional - 0.67) * 0.6;
    float cover = clamp(remap(weather, 1.0 - uCloudCoverage, 1.0 - uCloudCoverage + 0.35, 0.0, 1.0), 0.0, 1.0);
    if (cover <= 0.0) {
        return 0.0;
    }
    // Height profile: a fairly flat base (condensation level), rounded tops that rise with the
    // cover (the density fades out over a band, so the coverage threshold below rounds them).
    float profile = smoothstep(0.0, 0.15, h) * (1.0 - smoothstep(0.1 + 0.45 * cover, 0.35 + 0.65 * cover, h));
    if (profile <= 0.0) {
        return 0.0;
    }
    // Shape: Perlin-Worley "billows" (stretched to 0..1 by the measured range of the texture:
    // mean 0.67, 95 % within 0.4-0.9), sharpened by their own Worley octaves.
    vec4 n = texture(uCloudNoise, q / kCloudShapeTile);
    float worley = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float shape = clamp(remap(n.r, 0.45, 0.95, 0.0, 1.0) - (0.5 - worley) * 0.4, 0.0, 1.0);
    // Coverage carves the shape: only its densest part survives under thin cover.
    float density = clamp(remap(shape * profile, 1.0 - cover, 1.0, 0.0, 1.0), 0.0, 1.0) * cover;
    // The expected value is NOT the mean density spread evenly: a broken field of opaque clouds
    // over 50 % of the sky lets 50 % of the light through, while the same water spread evenly
    // would be opaque. So the far density is chosen to give the field's transmittance,
    // 1 - (cloud fraction), over the layer's thickness.
    float unresolved = smoothstep(0.4, 3.0, footprint);
    if (unresolved > 0.0) {
        float fraction = min(1.2 * cover * cover, 0.97);
        float thickness = (kCloudTop - kCloudBottom) * (0.35 + 0.65 * cover) * 0.5;
        float far = -log(1.0 - fraction) / (thickness * kCloudExtinction) * profile * 1.6;
        density = mix(density, far, unresolved);
    }
    if (density <= 0.0 || detail <= 0.0) {
        return density;
    }
    // Erosion by finer Worley noise: wispy at the base, billowy at the top.
    vec4 d = texture(uCloudNoise, q / kCloudDetailTile);
    float fine = d.g * 0.625 + d.b * 0.25 + d.a * 0.125;
    fine = mix(fine, 1.0 - fine, clamp(h * 4.0, 0.0, 1.0));
    return clamp(remap(density, fine * 0.35 * detail, 1.0, 0.0, 1.0), 0.0, 1.0);
}

float henyeyGreenstein(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

float cloudPhase(float c, float scale) {
    return mix(henyeyGreenstein(c, kCloudPhaseBackward * scale),
               henyeyGreenstein(c, kCloudPhaseForward * scale), kCloudPhaseBlend);
}

// Transmittance of sunlight through the clouds from atmosphere point p (km) towards the sun -
// for the terrain's cloud shadows. A few samples through the layer, no detail.
float cloudShadow(vec3 p, vec3 sun, vec3 eye) {
    if (uCloudsEnabled == 0) {
        return 1.0;
    }
    vec2 a;
    vec2 b;
    if (cloudIntervals(p, sun, a, b) == 0) {
        return 1.0;
    }
    float len = min(a.y - a.x, 20.0);
    float opticalDepth = 0.0;
    const int kSamples = 5;
    for (int i = 0; i < kSamples; ++i) {
        vec3 s = p + sun * (a.x + len * (float(i) + 0.5) / float(kSamples));
        opticalDepth += cloudDensity(s, s - eye + uCloudOrigin, 0.0, 0.0);
    }
    opticalDepth *= kCloudExtinction * len / float(kSamples);
    // Multiple scattering lets far more light through a cloud than Beer-Lambert alone, and
    // cloud shadows are soft: a floor and a gentle exponent.
    return mix(0.18, 1.0, exp(-0.25 * opticalDepth));
}
