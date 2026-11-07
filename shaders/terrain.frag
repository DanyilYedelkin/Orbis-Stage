in VsOut {
    vec3 cameraRelative;
    vec3 normal;
    float elevation;
} fsIn;

uniform vec3 uSunDirection; // unit, towards the sun
uniform vec3 uPlanetCenter; // planet centre relative to the camera, metres
uniform float uSunIlluminance;

uniform sampler2DArrayShadow uShadowMap;
uniform mat4 uLightMatrices[3]; // camera-relative -> light clip space, per cascade
uniform vec3 uCascadeSplits;    // far distance of each cascade, metres
uniform vec3 uShadowTexel;      // world size of one shadow texel, per cascade
uniform int uShadowsEnabled;

float sunVisibility(vec3 cameraRelative, vec3 normal, float viewDistance) {
    if (uShadowsEnabled == 0 || viewDistance >= uCascadeSplits.z) {
        return 1.0;
    }
    int c = viewDistance < uCascadeSplits.x ? 0 : (viewDistance < uCascadeSplits.y ? 1 : 2);
    // Normal offset scaled to the cascade's texel: no acne, no peter-panning.
    vec4 light = uLightMatrices[c] * vec4(cameraRelative + normal * (uShadowTexel[c] * 1.5), 1.0);
    vec3 uvz = vec3(light.xy * 0.5 + 0.5, light.z);
    if (any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0)))) {
        return 1.0;
    }
    // Rotated-grid 4 taps, each a hardware 2x2 PCF.
    const vec2 taps[4] = vec2[](vec2(-0.6, -1.4), vec2(1.4, -0.6), vec2(0.6, 1.4), vec2(-1.4, 0.6));
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
    float lit = 0.0;
    for (int i = 0; i < 4; ++i) {
        lit += texture(uShadowMap, vec4(uvz.xy + taps[i] * texel, float(c), uvz.z));
    }
    lit *= 0.25;
    return mix(lit, 1.0, smoothstep(uCascadeSplits.z * 0.8, uCascadeSplits.z, viewDistance));
}

uniform int uWireframe;
uniform int uShowLodLevels;
uniform int uLodLevel;

out vec4 fragColor;

// Linear albedos.
const vec3 kDeepWater    = vec3(0.004, 0.018, 0.070);
const vec3 kShallowWater = vec3(0.012, 0.090, 0.160);
const vec3 kLakeWater    = vec3(0.008, 0.030, 0.045);
const vec3 kSand         = vec3(0.420, 0.370, 0.250);
const vec3 kGrass        = vec3(0.055, 0.140, 0.030);
const vec3 kForest       = vec3(0.025, 0.065, 0.018);
const vec3 kTaiga        = vec3(0.022, 0.045, 0.025);
const vec3 kDry          = vec3(0.330, 0.240, 0.130);
const vec3 kTundra       = vec3(0.150, 0.135, 0.100);
const vec3 kMeadow       = vec3(0.110, 0.130, 0.050);
const vec3 kRock         = vec3(0.110, 0.095, 0.080);
const vec3 kSnow         = vec3(0.850, 0.870, 0.900);
const vec3 kSeaIce       = vec3(0.550, 0.600, 0.650);
const vec3 kDormant      = vec3(0.100, 0.085, 0.060);
const float kAmbientGroundAlbedo = 0.15;

// MODIS IGBP classes, see core/terrain/LandCover.hpp.
uniform usampler2D uLandCover;
uniform int uLandCoverEnabled;

const uint kClassOcean = 0u;
const uint kClassSnowIce = 15u;
const uint kClassBarren = 16u;
const uint kClassInlandWater = 17u;

// Summer appearance, typical visible-band reflectances.
const vec3 kClassAlbedo[18] = vec3[](
    vec3(0.0),                   //  0 ocean (unused: the sea is drawn from the elevation)
    vec3(0.018, 0.036, 0.020),   //  1 evergreen needleleaf forest
    vec3(0.020, 0.055, 0.016),   //  2 evergreen broadleaf forest
    vec3(0.030, 0.048, 0.024),   //  3 deciduous needleleaf forest (larch)
    vec3(0.028, 0.070, 0.020),   //  4 deciduous broadleaf forest
    vec3(0.024, 0.055, 0.020),   //  5 mixed forest
    vec3(0.090, 0.095, 0.050),   //  6 closed shrubland
    vec3(0.230, 0.185, 0.115),   //  7 open shrubland (semi-desert)
    vec3(0.050, 0.085, 0.030),   //  8 woody savanna
    vec3(0.140, 0.140, 0.060),   //  9 savanna
    vec3(0.085, 0.125, 0.040),   // 10 grassland
    vec3(0.035, 0.065, 0.040),   // 11 permanent wetland
    vec3(0.095, 0.100, 0.045),   // 12 cropland
    vec3(0.120, 0.112, 0.100),   // 13 urban
    vec3(0.065, 0.105, 0.035),   // 14 cropland / natural mosaic
    kSnow,                       // 15 snow and ice
    vec3(0.360, 0.270, 0.160),   // 16 barren (desert)
    kLakeWater);                 // 17 inland water

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i), n100 = hash13(i + vec3(1, 0, 0));
    float n010 = hash13(i + vec3(0, 1, 0)), n110 = hash13(i + vec3(1, 1, 0));
    float n001 = hash13(i + vec3(0, 0, 1)), n101 = hash13(i + vec3(1, 0, 1));
    float n011 = hash13(i + vec3(0, 1, 1)), n111 = hash13(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z) * 2.0 - 1.0;
}

// Leaf-off or dormant (winter, tropical dry season).
const vec3 kClassDormant[18] = vec3[](
    vec3(0.0),
    vec3(0.016, 0.028, 0.019),
    vec3(0.020, 0.050, 0.016),
    vec3(0.040, 0.038, 0.032),   // bare larch
    vec3(0.050, 0.044, 0.034),   // trunks and litter
    vec3(0.032, 0.040, 0.026),
    vec3(0.085, 0.080, 0.055),
    vec3(0.230, 0.185, 0.115),
    vec3(0.080, 0.080, 0.045),
    vec3(0.200, 0.170, 0.100),   // straw
    vec3(0.170, 0.145, 0.090),
    vec3(0.070, 0.065, 0.045),
    vec3(0.120, 0.095, 0.070),   // stubble and bare soil
    vec3(0.120, 0.112, 0.100),
    vec3(0.110, 0.095, 0.065),
    kSnow,
    vec3(0.360, 0.270, 0.160),
    kLakeWater);

// Autumn foliage; the summer colour where leaves do not turn.
const vec3 kClassAutumn[18] = vec3[](
    vec3(0.0),
    vec3(0.018, 0.036, 0.020),
    vec3(0.020, 0.055, 0.016),
    vec3(0.110, 0.085, 0.020),   // golden larch
    vec3(0.100, 0.050, 0.018),   // rust and orange
    vec3(0.060, 0.045, 0.020),
    vec3(0.090, 0.095, 0.050),
    vec3(0.230, 0.185, 0.115),
    vec3(0.050, 0.085, 0.030),
    vec3(0.140, 0.140, 0.060),
    vec3(0.085, 0.125, 0.040),
    vec3(0.035, 0.065, 0.040),
    vec3(0.095, 0.100, 0.045),
    vec3(0.120, 0.112, 0.100),
    vec3(0.065, 0.105, 0.035),
    kSnow,
    vec3(0.360, 0.270, 0.160),
    kLakeWater);

// How much lying snow the canopy hides from above.
const float kClassCanopy[18] = float[](
    0.0, 0.8, 0.8, 0.6, 0.55, 0.7, 0.3, 0.0, 0.4, 0.1, 0.0, 0.2, 0.0, 0.3, 0.2, 0.0, 0.0, 0.0);

// Monthly means from climate.glsl drive phenology, snow and ice. Thresholds: growing season
// above ~5 C, leaves keep longer in autumn than they take to come out in spring, snow lies where
// the month is below freezing.
struct Season {
    float temperature;
    float growth;  // 0 dormant .. 1 in full leaf
    float autumn;
    float snow;
};

Season seasonAt(vec3 unitDir, float latitude, float longitude, float h, float breakup) {
    Season s;
    s.temperature = climateTemperature(latitude, longitude, max(h, 0.0));
    // Patches of ~7 and ~1.5 km from noise on the sphere: across a snowy plain the tiling
    // detail texture would print its period.
    float patches = 0.6 * valueNoise(unitDir * 900.0) + 0.4 * valueNoise(unitDir * 4100.0);
    float t = s.temperature + 2.5 * patches + 0.4 * breakup;
    float cooling = clamp(climateCooling(latitude), 0.0, 1.0);
    s.growth = smoothstep(mix(3.0, 2.0, cooling), mix(10.0, 8.0, cooling), t);
    // Savannas and grasslands of the outer tropics brown in the dry season (local winter).
    float latDeg = degrees(abs(latitude));
    float dryBelt = smoothstep(6.0, 12.0, latDeg) * (1.0 - smoothstep(22.0, 30.0, latDeg));
    s.growth *= 1.0 - dryBelt * smoothstep(0.0, 0.6, climateWinterness(latitude));
    s.autumn = smoothstep(0.2, 0.6, cooling) * (1.0 - smoothstep(11.0, 16.0, t));
    s.snow = smoothstep(0.0, -5.0, t);
    return s;
}

vec3 classAlbedo(uint cls, Season season) {
    vec3 leaves = mix(kClassAlbedo[cls], kClassAutumn[cls], season.autumn);
    return mix(kClassDormant[cls], leaves, season.growth);
}

struct Cover {
    vec3 albedo; // weighted by `land`
    float canopy; // weighted by `land`
    float land;
    float ocean;
    float lake;
    float snow;
    float barren;
};

// Classes are categorical, so the 4 surrounding cells are fetched and their materials blended.
// The lookup is domain-warped to turn the 5.6 km grid into irregular boundaries.
Cover landCoverAt(vec3 unitDir, float latitude, float longitude, Season season) {
    vec2 size = vec2(textureSize(uLandCover, 0));
    // ~25, 6, 1.5 and 0.4 km wavelengths (unit direction * Earth radius / wavelength).
    vec3 q = unitDir * 255.0;
    vec2 warp = vec2(valueNoise(q), valueNoise(q + 17.3)) * 0.6 +
                vec2(valueNoise(q * 4.1 + 3.1), valueNoise(q * 4.1 - 8.7)) * 0.45 +
                vec2(valueNoise(q * 16.7 - 5.3), valueNoise(q * 16.7 + 1.9)) * 0.3 +
                vec2(valueNoise(q * 63.1 + 9.2), valueNoise(q * 63.1 - 4.4)) * 0.15;
    vec2 cell = vec2((longitude + kPi) / (2.0 * kPi) * size.x,
                     (0.5 * kPi - latitude) / kPi * size.y) - 0.5 + warp;
    ivec2 i0 = ivec2(floor(cell));
    vec2 f = smoothstep(0.15, 0.85, fract(cell));
    ivec2 isize = ivec2(size);

    Cover c = Cover(vec3(0.0), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1);
        ivec2 t = ivec2((i0.x + o.x + isize.x) % isize.x, clamp(i0.y + o.y, 0, isize.y - 1));
        uint cls = texelFetch(uLandCover, t, 0).r;
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        if (cls == kClassOcean) {
            c.ocean += w;
        } else if (cls == kClassInlandWater) {
            c.lake += w;
        } else {
            c.albedo += classAlbedo(min(cls, 17u), season) * w;
            c.canopy += kClassCanopy[min(cls, 17u)] * w;
            c.land += w;
            c.snow += cls == kClassSnowIce ? w : 0.0;
            c.barren += cls == kClassBarren ? w : 0.0;
        }
    }
    return c;
}

// Fallback without land cover (procedural planet, --no-landcover).
vec3 stylisedLowland(float h, float latitude, out float arid) {
    arid = smoothstep(12.0, 19.0, latitude) * (1.0 - smoothstep(31.0, 38.0, latitude));
    float boreal = smoothstep(52.0, 60.0, latitude);
    float tundra = smoothstep(63.0, 70.0, latitude);
    vec3 lowland = mix(kGrass, kForest, smoothstep(150.0, 900.0, h));
    lowland = mix(lowland, kTaiga, boreal);
    lowland = mix(lowland, kDry, arid * 0.85);
    return mix(lowland, kTundra, tundra);
}

// Surface detail: one tiling texture (core/terrain/DetailNoise.hpp) at four world scales.
// q = cameraRelative + camera mod kDetailPeriod (computed in double), so the detail is anchored
// to the world with small floats; every tile size divides the period, so the wrap is invisible.
uniform sampler2D uDetailNoise;
uniform vec3 uDetailOrigin;
uniform int uSurfaceDetail;
uniform float uDetailGradientScale; // 1 / std of the stored derivatives

const float kDetailPeriod = 256.0;
const float kDetailTiles[4] = float[](256.0, 32.0, 4.0, 0.5);
// Std of the slope each scale adds on bare rock. Slopes rather than heights: the finest
// octaves dominate a gradient, so heights would tie the look to the octave count.
const float kBumpSlope[4] = float[](0.0, 0.30, 0.22, 0.14);

struct Detail {
    float height[4]; // mean 0, std 1; 0 where faded out
    float variation[4];
    vec3 gradient[4]; // world space, std 1
};

Detail sampleDetail(vec3 q, vec3 n, float viewDistance) {
    vec3 w = pow(abs(n), vec3(4.0));
    w /= w.x + w.y + w.z;
    Detail d;
    for (int i = 0; i < 4; ++i) {
        float tile = kDetailTiles[i];
        // Gone before a tile shrinks below ~20 px, where its repetition would read as a grid.
        float fade = 1.0 - smoothstep(tile * 30.0, tile * 120.0, viewDistance);
        d.height[i] = 0.0;
        d.variation[i] = 0.0;
        d.gradient[i] = vec3(0.0);
        if (fade <= 0.0) {
            continue;
        }
        vec2 offset = vec2(0.371, 0.613) * float(i);
        vec4 x = texture(uDetailNoise, q.yz / tile + offset);
        vec4 y = texture(uDetailNoise, q.zx / tile + offset);
        vec4 z = texture(uDetailNoise, q.xy / tile + offset);
        d.height[i] = (x.r * w.x + y.r * w.y + z.r * w.z) * fade;
        d.variation[i] = (x.a * w.x + y.a * w.y + z.a * w.z) * fade;
        d.gradient[i] = (vec3(0.0, x.g, x.b) * w.x + vec3(y.b, 0.0, y.g) * w.y +
                         vec3(z.g, z.b, 0.0) * w.z) * (fade * uDetailGradientScale);
    }
    return d;
}

struct Surface {
    vec3 albedo;
    float bump;  // rock 1
    float water; // lakes
    float snow;
};

// Land cover gives the lowland; altitudinal zones and cliffs come from elevation and slope,
// which 5.6 km cells cannot resolve. `breakup` (std ~1) frays every threshold.
Surface terrainSurface(float h, float slope, vec3 unitDir, float latitude, float longitude,
                       float breakup) {
    Surface s = Surface(vec3(0.0), 0.0, 0.0, 0.0);
    if (h < 0.0) {
        float ice = smoothstep(-1.5, -5.0, climateTemperature(latitude, longitude, 0.0) +
                                               2.0 * breakup);
        s.albedo = mix(mix(kDeepWater, kShallowWater, smoothstep(-2500.0, 0.0, h)), kSeaIce, ice);
        s.water = 1.0 - ice;
        s.bump = 0.15 * ice;
        return s;
    }
    Season season = seasonAt(unitDir, latitude, longitude, h, breakup);
    float latDeg = degrees(abs(latitude));
    float arid;
    vec3 lowland = stylisedLowland(h, latDeg, arid);
    lowland = mix(mix(lowland, kDormant, 0.7), lowland, season.growth);
    float canopy = 0.3;
    float coast = smoothstep(0.0, 8.0, h + 3.0 * breakup);
    float glacier = 0.0;
    float lake = 0.0;
    if (uLandCoverEnabled != 0) {
        Cover cover = landCoverAt(unitDir, latitude, longitude, season);
        if (cover.land > 0.0) {
            vec3 measured = cover.albedo / cover.land;
            canopy = cover.canopy / cover.land;
            // "Barren" is sand only in the dry belts; elsewhere it is polar rock or alpine scree
            // (MODIS marks most of the Matterhorn's surroundings barren).
            float polar = smoothstep(55.0, 68.0, latDeg);
            float alpine = smoothstep(800.0, 1600.0, h) * (1.0 - arid);
            measured = mix(measured, kRock * 1.4, max(polar, alpine) * cover.barren / cover.land);
            measured = mix(measured, kTundra, polar * 0.6 * (1.0 - cover.barren / cover.land));
            lowland = mix(lowland, measured, cover.land / (cover.land + cover.lake + 1e-4));
            arid = cover.barren / cover.land;
            glacier = cover.snow / cover.land;
        }
        lake = smoothstep(0.35, 0.65, cover.lake + 0.12 * breakup);
        // Beaches only near the sea: the Netherlands are below 8 m and not a beach.
        coast = 1.0 - (1.0 - smoothstep(0.0, 6.0, h + 2.0 * breakup)) * smoothstep(0.0, 0.5, cover.ocean);
    }

    // Summer snow line fitted to ~5 km (tropics), 3.3 km (Alps), 2.9 km (Tatras), 1.4 km
    // (Scandinavia), sea level beyond ~75 deg; tree and rock lines follow 1.3 / 0.7 km below.
    float snowLine = 5000.0 - 5200.0 * smoothstep(25.0, 80.0, latDeg);
    float treeLine = snowLine - 1300.0 + 120.0 * breakup;
    float rockLine = snowLine - 700.0 + 150.0 * breakup;

    float sand = 1.0 - coast;
    float meadow = smoothstep(treeLine - 250.0, treeLine + 150.0, h) * (1.0 - arid);
    float rock = max(smoothstep(rockLine - 200.0, rockLine + 300.0, h),
                     smoothstep(0.22, 0.42, slope + 0.07 * breakup));
    snowLine += 1800.0 * slope + 200.0 * breakup;
    float snow = max(smoothstep(snowLine, snowLine + 350.0, h),
                     glacier * (1.0 - smoothstep(0.35, 0.6, slope + 0.05 * breakup)));
    // Seasonal snow slides off steep rock and is partly hidden by trees; lakes freeze over.
    float seasonal = season.snow * (1.0 - smoothstep(0.45, 0.7, slope + 0.05 * breakup));
    snow = max(snow, seasonal * (1.0 - 0.75 * canopy * (1.0 - rock)));
    snow = max(snow, lake * smoothstep(-1.0, -4.0, season.temperature + breakup));

    vec3 c = mix(lowland, kSand, sand);
    c = mix(c, kMeadow, meadow);
    c = mix(c, kRock, rock);
    c = mix(c, kSnow, snow);
    float water = lake * (1.0 - snow);
    s.albedo = mix(c, kLakeWater, water);
    s.water = water;
    s.snow = snow;
    s.bump = mix(mix(mix(0.4, 0.25, sand), 1.0, rock), 0.2, snow) * (1.0 - water);
    return s;
}

void main() {
    if (uWireframe != 0) {
        fragColor = vec4(0.2, 1.0, 0.5, 1.0);
        return;
    }

    vec3 geometricNormal = normalize(fsIn.normal);
    vec3 up = normalize(fsIn.cameraRelative - uPlanetCenter);
    vec3 toCamera = normalize(-fsIn.cameraRelative);
    float viewDistance = length(fsIn.cameraRelative);
    float slope = 1.0 - clamp(dot(geometricNormal, up), 0.0, 1.0);

    // Geodetic latitude: tan(lat) = z (a/b)^2 / p.
    vec3 planetPosition = fsIn.cameraRelative - uPlanetCenter;
    vec3 unitDir = normalize(planetPosition);
    float latitude = atan(planetPosition.z * kEllipsoidAOverB * kEllipsoidAOverB,
                          length(planetPosition.xy));
    float longitude = atan(planetPosition.y, planetPosition.x);

    Detail detail;
    float breakup = 0.0;
    if (uSurfaceDetail != 0) {
        detail = sampleDetail(fsIn.cameraRelative + uDetailOrigin, geometricNormal, viewDistance);
        breakup = 0.45 * detail.height[0] + 0.35 * detail.height[1] + 0.2 * detail.height[2];
    }
    Surface surface = terrainSurface(fsIn.elevation, slope, unitDir, latitude, longitude, breakup);
    vec3 albedo = surface.albedo;
    vec3 n = geometricNormal;
    float cavity = 1.0;
    if (uSurfaceDetail != 0 && fsIn.elevation >= 0.0) {
        // The broadest variation (3 km, 700 m) is noise on the sphere, not the tiling texture:
        // from 50 km its repetition would show.
        float macro = 0.65 * valueNoise(unitDir * 2120.0) + 0.45 * valueNoise(unitDir * 9100.0) +
                      0.3 * detail.variation[0];
        float vegetation = clamp(albedo.g / max(albedo.r, 1e-3) - 1.0, 0.0, 1.0);
        albedo *= exp((0.22 * macro + 0.10 * detail.variation[1]) * (1.0 - 0.6 * surface.snow));
        albedo = mix(albedo, albedo.ggg * vec3(1.25, 1.0, 0.55),
                     0.35 * vegetation * smoothstep(0.3, 1.5, macro));
        float fine = 0.10 * detail.height[1] + 0.12 * detail.height[2] + 0.08 * detail.height[3];
        albedo *= clamp(1.0 + fine * (0.45 + 0.8 * surface.bump), 0.5, 1.5);

        vec3 gradient = vec3(0.0);
        for (int i = 1; i < 4; ++i) {
            gradient += detail.gradient[i] * kBumpSlope[i];
        }
        gradient *= surface.bump;
        n = normalize(geometricNormal - (gradient - dot(gradient, geometricNormal) * geometricNormal));
        cavity = clamp(1.0 + 0.12 * surface.bump * (detail.height[1] + detail.height[2]), 0.7, 1.0);
    }

    if (uShowLodLevels != 0) {
        float hue = fract(float(uLodLevel) * 0.618034);
        vec3 tint = clamp(abs(fract(hue + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0,
                          0.0, 1.0);
        albedo = mix(albedo, tint * 0.5, 0.65);
    }
    float nDotL = max(dot(n, uSunDirection), 0.0);
    // Bumps must not light faces the geometry turns away (speckles in the shadow map's dark).
    nDotL *= smoothstep(-0.05, 0.1, dot(geometricNormal, uSunDirection));

    vec3 p = toAtmosphere(planetPosition);
    vec3 eye = toAtmosphere(-uPlanetCenter);
    float r = max(length(p), kBottomRadius + 0.001);
    vec3 sunA = directionToAtmosphere(uSunDirection);
    float muSun = dot(normalize(p), sunA);

    vec3 sunlight = uSunIlluminance * sunTransmittance(r, muSun);
    float visibility = sunVisibility(fsIn.cameraRelative, geometricNormal, viewDistance) *
                       cloudShadow(p, sunA, eye);
    vec3 color = albedo / kPi * sunlight * nDotL * visibility;

    // The table is for a horizontal surface; a tilted one sees (1 + n.up) / 2 of the sky and
    // sunlit ground for the rest.
    vec3 skyE = skyIrradiance(r, muSun);
    vec3 groundE = kAmbientGroundAlbedo * (skyE + sunTransmittance(r, muSun) * max(muSun, 0.0));
    float skyView = 0.5 + 0.5 * dot(n, up);
    color += albedo / kPi * uSunIlluminance * (skyE * skyView + groundE * (1.0 - skyView)) * cavity;

    if (surface.water > 0.5) {
        vec3 halfVector = normalize(uSunDirection + toCamera);
        color += sunlight * pow(max(dot(geometricNormal, halfVector), 0.0), 600.0) * 0.5 * nDotL *
                 visibility;
    }

    // Aerial perspective: the same integral as the sky.
    vec3 toFragment = p - eye;
    float fragmentDistance = length(toFragment);
    vec3 viewDir = toFragment / fragmentDistance;
    vec2 top = raySphere(eye, viewDir, kTopRadius);
    float t0 = max(top.x, 0.0);
    float t1 = min(fragmentDistance, top.y);
    if (t1 > t0) {
        vec3 inScattered;
        vec3 transmittance;
        integrateScattering(eye, viewDir, t0, t1, sunA, 12, inScattered, transmittance);
        color = color * transmittance + inScattered * uSunIlluminance;
    }

    fragColor = vec4(color, 1.0);
}
