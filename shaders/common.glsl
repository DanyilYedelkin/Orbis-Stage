// ACES fit (Narkowicz), then gamma 2.2.
vec3 toDisplay(vec3 linearColor) {
    vec3 x = linearColor * 0.8; // exposure
    vec3 mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return pow(mapped, vec3(1.0 / 2.2));
}
