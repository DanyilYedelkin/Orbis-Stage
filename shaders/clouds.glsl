// Cloud density, shared by the cloud passes and the terrain (shadows). Constants come from
// CloudParameters; needs the atmosphere prelude. Units: km.

uniform samplerCube uCloudWeather; // cloudiness 0..1
uniform sampler3D uCloudNoise;     // R: Perlin-Worley, GBA: Worley octaves; tiling
uniform vec3 uCloudOrigin;         // (eye - wind offset) mod kCloudRegionalTile
uniform vec3 uCloudDetailShift;    // extra drift of the detail, mod kCloudDetailTile
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

// Twin of rayShellIntervals() in Clouds.cpp, which is tested.
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
        candidates[0] = vec2(o.x, min(o.y, stop));
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

// `p`: atmosphere-model position. `q`: the same point in noise coordinates (p - eye + origin).
// `detail` 0 skips the erosion. `footprint`: pixel size at p, km.
float cloudDensity(vec3 p, vec3 q, float detail, float footprint) {
    float r = length(p);
    float h = (r - kBottomRadius - kCloudBottom) / (kCloudTop - kCloudBottom);
    if (h <= 0.0 || h >= 1.0) {
        return 0.0;
    }
    // A coarse look-up of the shape noise breaks up the repetition of the 12 km tiles.
    float weather = texture(uCloudWeather, p / r).r;
    float regional = texture(uCloudNoise, q / kCloudRegionalTile).r;
    weather += (regional - 0.67) * 0.6;
    float cover = clamp(remap(weather, 1.0 - uCloudCoverage, 1.0 - uCloudCoverage + 0.35, 0.0, 1.0), 0.0, 1.0);
    if (cover <= 0.0) {
        return 0.0;
    }
    // Flat bases, rounded tops that rise with the cover.
    float profile = smoothstep(0.0, 0.15, h) * (1.0 - smoothstep(0.1 + 0.45 * cover, 0.35 + 0.65 * cover, h));
    if (profile <= 0.0) {
        return 0.0;
    }
    // 0.45..0.95 is the measured range of the Perlin-Worley channel.
    vec4 n = texture(uCloudNoise, q / kCloudShapeTile);
    float worley = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float shape = clamp(remap(n.r, 0.45, 0.95, 0.0, 1.0) - (0.5 - worley) * 0.4, 0.0, 1.0);
    float density = clamp(remap(shape * profile, 1.0 - cover, 1.0, 0.0, 1.0), 0.0, 1.0) * cover;
    // Below a pixel, thresholding the mip-filtered noise turns cloudy regions into opaque sheets.
    // Use a density that gives the field's average transmittance instead: half the sky covered
    // lets half the light through, which the same water spread evenly would not.
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
    // Wispy at the base, billowy at the top.
    vec4 d = texture(uCloudNoise, (q + uCloudDetailShift) / kCloudDetailTile);
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

// Sunlight transmittance from p through the layer, for the terrain.
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
    // Multiple scattering lets far more light through than Beer-Lambert.
    return mix(0.18, 1.0, exp(-0.25 * opticalDepth));
}
