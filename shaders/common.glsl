// Shared by sky and terrain.

// HDR -> display. Lighting produces values above 1 (a sunlit snowfield is ~2); clamping would
// burn them to flat white. The ACES filmic curve (K. Narkowicz's fit) compresses highlights
// smoothly, then we encode for the monitor (linear -> sRGB, gamma 2.2 approximation).
vec3 toDisplay(vec3 linearColor) {
    vec3 x = linearColor * 0.8; // exposure
    vec3 mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return pow(mapped, vec3(1.0 / 2.2));
}
