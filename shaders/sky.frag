in vec2 vNdc;

uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTanHalfFov;
uniform vec3 uCameraPlanet;   // ECEF, metres
uniform vec3 uSunDirection;
uniform float uSunIlluminance;

out vec4 fragColor;

void main() {
    vec3 dirWorld = normalize(uForward + vNdc.x * uTanHalfFov.x * uRight + vNdc.y * uTanHalfFov.y * uUp);

    vec3 origin = toAtmosphere(uCameraPlanet);
    vec3 dir = directionToAtmosphere(dirWorld);
    vec3 sun = directionToAtmosphere(uSunDirection);

    vec3 color = vec3(0.0);
    vec3 viewTransmittance = vec3(1.0);
    vec2 top = raySphere(origin, dir, kTopRadius);
    if (top.y > 0.0) {
        float t0 = max(top.x, 0.0);
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

    // Far below the real brightness: saturating all channels would turn the sunset sun white.
    float disk = smoothstep(0.99996, 0.99999, dot(dirWorld, uSunDirection));
    color += viewTransmittance * disk * uSunIlluminance * 6.0;

    fragColor = vec4(color, 1.0);
}
