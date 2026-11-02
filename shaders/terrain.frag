#version 450 core

in VsOut {
    vec3 cameraRelative;
    vec3 normal;
    float elevation;
} fsIn;

uniform vec3 uSunDirection; // unit, towards the sun
uniform vec3 uPlanetCenter; // planet centre relative to the camera
uniform int uWireframe;

out vec4 fragColor;

// All colours are LINEAR. Lighting maths is only correct in linear space; we convert to sRGB
// (the monitor's encoding) once, at the very end.
const vec3 kDeepWater    = vec3(0.004, 0.018, 0.070);
const vec3 kShallowWater = vec3(0.010, 0.080, 0.150);
const vec3 kSand         = vec3(0.450, 0.400, 0.260);
const vec3 kGrass        = vec3(0.060, 0.160, 0.030);
const vec3 kForest       = vec3(0.030, 0.080, 0.020);
const vec3 kRock         = vec3(0.180, 0.150, 0.120);
const vec3 kSnow         = vec3(0.850, 0.870, 0.900);

vec3 terrainAlbedo(float h, float slope, float absLatitudeSin) {
    if (h < 0.0) {
        return mix(kDeepWater, kShallowWater, smoothstep(-4000.0, 0.0, h));
    }
    vec3 c = mix(kSand, kGrass, smoothstep(0.0, 120.0, h));
    c = mix(c, kForest, smoothstep(300.0, 1200.0, h));
    c = mix(c, kRock, smoothstep(1800.0, 3200.0, h));
    c = mix(c, kRock, smoothstep(0.25, 0.45, slope)); // steep => bare rock
    // Snow line drops from ~4.5 km at the equator to sea level near the poles,
    // and snow does not stick to cliffs.
    float snowLine = mix(4500.0, -200.0, smoothstep(0.75, 0.97, absLatitudeSin));
    snowLine += 1500.0 * slope;
    return mix(c, kSnow, smoothstep(snowLine, snowLine + 500.0, h));
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
    float nDotL = max(dot(n, uSunDirection), 0.0);
    // Soft day/night terminator: the sky still lights the ground a little after sunset.
    float daylight = smoothstep(-0.15, 0.25, dot(up, uSunDirection));

    vec3 sunColor = vec3(1.0, 0.96, 0.90) * 2.2;
    vec3 skyColor = vec3(0.35, 0.50, 0.80) * 0.25;
    vec3 color = albedo * (sunColor * nDotL + skyColor * daylight);

    // Sun glint on water (Blinn-Phong).
    if (fsIn.elevation < 0.0) {
        vec3 halfVector = normalize(uSunDirection + toCamera);
        color += vec3(1.0, 0.95, 0.85) * pow(max(dot(n, halfVector), 0.0), 600.0) * 1.5 * nDotL;
    }

    // Cheap atmosphere hint: surfaces seen at grazing angles fade towards sky blue.
    // (A physically based atmosphere is planned for a later stage.)
    float grazing = pow(1.0 - clamp(dot(up, toCamera), 0.0, 1.0), 5.0);
    color = mix(color, vec3(0.30, 0.50, 1.00) * (0.15 + 0.85 * daylight), grazing * 0.6);

    // Linear -> sRGB (good approximation of the exact piecewise curve).
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
