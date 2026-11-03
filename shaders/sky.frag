in vec2 vNdc;

uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTanHalfFov;     // (tan(fovX / 2), tan(fovY / 2))
uniform vec3 uCameraPlanet;   // camera position relative to the planet centre, metres
uniform vec3 uSunDirection;   // unit, ECEF
uniform float uSunIlluminance;

out vec4 fragColor;

void main() {
    // Reconstruct the view ray of this pixel from the camera basis.
    vec3 dirWorld = normalize(uForward + vNdc.x * uTanHalfFov.x * uRight + vNdc.y * uTanHalfFov.y * uUp);

    vec3 origin = toAtmosphere(uCameraPlanet);
    vec3 dir = directionToAtmosphere(dirWorld);
    vec3 sun = directionToAtmosphere(uSunDirection);

    vec3 color = vec3(0.0);
    vec3 viewTransmittance = vec3(1.0);
    vec2 top = raySphere(origin, dir, kTopRadius);
    if (top.y > 0.0) { // the ray passes through the atmosphere
        float t0 = max(top.x, 0.0); // from space: start where the ray enters
        float t1 = top.y;
        vec2 ground = raySphere(origin, dir, kBottomRadius);
        bool hitsGround = ground.x > 0.0;
        if (hitsGround) {
            t1 = ground.x;
        }
        vec3 luminance;
        vec3 transmittance;
        integrateScattering(origin, dir, t0, t1, sun, 32, luminance, transmittance);
        color = luminance * uSunIlluminance;
        viewTransmittance = hitsGround ? vec3(0.0) : transmittance;
    }

    // Sun disk (~0.5 degrees), dimmed and reddened by the air in front of it. Kept far below
    // its real brightness: otherwise all three channels saturate in the tone mapper and the
    // sunset sun loses its colour (it would render white at any elevation).
    float disk = smoothstep(0.99996, 0.99999, dot(dirWorld, uSunDirection));
    color += viewTransmittance * disk * uSunIlluminance * 6.0;

    fragColor = vec4(toDisplay(color), 1.0);
}
