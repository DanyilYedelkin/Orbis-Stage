in VsOut {
    vec3 cameraRelative;
    vec3 normal;
    float elevation;
} fsIn;

uniform vec3 uSunDirection; // unit, towards the sun
uniform vec3 uPlanetCenter; // planet centre relative to the camera
uniform int uWireframe;
uniform int uShowLodLevels; // debug: tint by quadtree level
uniform int uLodLevel;

out vec4 fragColor;

// All colours are LINEAR. Lighting maths is only correct in linear space; we convert to sRGB
// (the monitor's encoding) once, at the very end.
const vec3 kDeepWater    = vec3(0.004, 0.018, 0.070);
const vec3 kShallowWater = vec3(0.012, 0.090, 0.160);
const vec3 kSand         = vec3(0.420, 0.370, 0.250);
const vec3 kGrass        = vec3(0.055, 0.140, 0.030);
const vec3 kForest       = vec3(0.025, 0.065, 0.018);
const vec3 kTaiga        = vec3(0.022, 0.045, 0.025);
const vec3 kDry          = vec3(0.330, 0.240, 0.130);
const vec3 kTundra       = vec3(0.150, 0.135, 0.100);
const vec3 kMeadow       = vec3(0.110, 0.130, 0.050);
const vec3 kRock         = vec3(0.110, 0.095, 0.080);
const vec3 kSnow         = vec3(0.850, 0.870, 0.900);

// Land cover is a STYLISATION driven by elevation, slope and latitude (no land-cover data yet):
// arid subtropical belts, temperate forest, taiga, tundra, then rock and snow with height.
vec3 terrainAlbedo(float h, float slope, float absLatitudeSin) {
    if (h < 0.0) {
        // Continental shelves (shallow) read clearly lighter than the deep ocean.
        return mix(kDeepWater, kShallowWater, smoothstep(-2500.0, 0.0, h));
    }
    float latitude = degrees(asin(absLatitudeSin));
    float arid = smoothstep(12.0, 19.0, latitude) * (1.0 - smoothstep(31.0, 38.0, latitude));
    float boreal = smoothstep(52.0, 60.0, latitude);
    float tundra = smoothstep(63.0, 70.0, latitude);

    vec3 lowland = mix(kGrass, kForest, smoothstep(150.0, 900.0, h));
    lowland = mix(lowland, kTaiga, boreal);
    lowland = mix(lowland, kDry, arid * 0.85);
    lowland = mix(lowland, kTundra, tundra);

    // Summer snow line by latitude, fitted to real reference points: ~5 km in the tropics,
    // ~3.3 km in the Alps (46 deg), ~2.9 km in the Tatras (49), ~1.4 km in Scandinavia (60),
    // sea level beyond ~75 deg. The tree line and the rock line follow it, as in nature:
    // forest ends ~1.3 km below the snow, bare rock starts ~0.7 km below it.
    float snowLine = 5000.0 - 5200.0 * smoothstep(25.0, 80.0, latitude);
    float treeLine = snowLine - 1300.0;
    float rockLine = snowLine - 700.0;

    vec3 c = mix(kSand, lowland, smoothstep(0.0, 8.0, h)); // a thin beach, not sandy lowlands
    c = mix(c, kMeadow, smoothstep(treeLine - 250.0, treeLine + 150.0, h) * (1.0 - arid));
    c = mix(c, kRock, smoothstep(rockLine - 200.0, rockLine + 300.0, h));
    c = mix(c, kRock, smoothstep(0.22, 0.42, slope)); // steep => bare rock at any height

    snowLine += 1800.0 * slope; // snow does not stick to cliffs
    return mix(c, kSnow, smoothstep(snowLine, snowLine + 350.0, h));
}

void main() {
    if (uWireframe != 0) {
        fragColor = vec4(0.2, 1.0, 0.5, 1.0);
        return;
    }

    vec3 n = normalize(fsIn.normal);
    vec3 up = normalize(fsIn.cameraRelative - uPlanetCenter); // local vertical (approx.)
    vec3 toCamera = normalize(-fsIn.cameraRelative);
    float slope = 1.0 - clamp(dot(n, up), 0.0, 1.0);

    // ECEF +Z is the polar axis, so up.z is (almost exactly) sin(latitude).
    vec3 albedo = terrainAlbedo(fsIn.elevation, slope, abs(up.z));
    if (uShowLodLevels != 0) {
        // Golden-angle hue steps: neighbouring levels always get clearly different colours.
        float hue = fract(float(uLodLevel) * 0.618034);
        vec3 tint = clamp(abs(fract(hue + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0,
                          0.0, 1.0);
        albedo = mix(albedo, tint * 0.5, 0.65);
    }
    float nDotL = max(dot(n, uSunDirection), 0.0);

    vec3 sunColor = vec3(1.0, 0.96, 0.90) * 2.2;
    float day = daylight(dot(up, uSunDirection));
    vec3 color = albedo * (sunColor * nDotL + kZenithSky * 0.5 * day);

    // Sun glint on water (Blinn-Phong).
    if (fsIn.elevation < 0.0) {
        vec3 halfVector = normalize(uSunDirection + toCamera);
        color += vec3(1.0, 0.95, 0.85) * pow(max(dot(n, halfVector), 0.0), 600.0) * 1.5 * nDotL;
    }

    // Aerial perspective. What matters is how much AIR lies between the eye and the surface:
    // from orbit looking down that is only the ~8 km thick lower atmosphere (a light tint),
    // at the limb or near the ground at a shallow angle it is the whole distance (strong haze).
    float dist = length(fsIn.cameraRelative);
    float cosView = max(dot(up, toCamera), 0.02);
    float airPath = min(dist, 8000.0 / cosView);
    float haze = 1.0 - exp(-airPath / 120000.0);
    vec3 hazeColor = kHorizonSky * day;
    color = mix(color, hazeColor, haze);

    fragColor = vec4(toDisplay(color), 1.0);
}
