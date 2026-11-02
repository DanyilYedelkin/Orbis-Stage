// Shared by sky and terrain so the haze on distant terrain matches the sky behind it exactly.
// (Prepended to both fragment shaders at load time.)

const vec3 kHorizonSky = vec3(0.50, 0.66, 0.90);
const vec3 kZenithSky  = vec3(0.08, 0.22, 0.60);

// How much atmosphere surrounds the camera: 1 on the ground, ~0 in orbit.
float atmosphereFactor(float altitude) {
    return exp(-max(altitude, 0.0) / 25000.0);
}

// Brightness of the sky for a given sun elevation (sin) at a point: soft dusk, black night.
float daylight(float sunElevationSin) {
    return smoothstep(-0.12, 0.20, sunElevationSin);
}

vec3 skyColor(vec3 dir, vec3 up, vec3 sun, float altitude) {
    float elevation = dot(dir, up);
    float day = daylight(dot(up, sun));
    vec3 gradient = mix(kHorizonSky, kZenithSky, smoothstep(0.0, 0.55, elevation));
    // Warmer horizon around sunrise / sunset, towards the sun.
    float dusk = (1.0 - smoothstep(0.0, 0.35, dot(up, sun))) * pow(max(dot(dir, sun), 0.0), 3.0);
    gradient = mix(gradient, vec3(1.0, 0.55, 0.30), dusk * 0.6 * smoothstep(-0.05, 0.1, elevation));
    return gradient * day * atmosphereFactor(altitude);
}

// HDR -> display. Lighting produces values above 1 (a sunlit snowfield is ~2); clamping would
// burn them to flat white. The ACES filmic curve (K. Narkowicz's fit) compresses highlights
// smoothly, then we encode for the monitor (linear -> sRGB, gamma 2.2 approximation).
vec3 toDisplay(vec3 linearColor) {
    vec3 x = linearColor * 0.8; // exposure
    vec3 mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return pow(mapped, vec3(1.0 / 2.2));
}
