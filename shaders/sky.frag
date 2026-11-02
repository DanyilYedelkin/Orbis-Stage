in vec2 vNdc;

uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTanHalfFov;   // (tan(fovX / 2), tan(fovY / 2))
uniform vec3 uLocalUp;      // geodetic up at the camera
uniform vec3 uSunDirection;
uniform float uAltitude;

out vec4 fragColor;

void main() {
    // Reconstruct the view ray of this pixel from the camera basis.
    vec3 dir = normalize(uForward + vNdc.x * uTanHalfFov.x * uRight + vNdc.y * uTanHalfFov.y * uUp);
    vec3 color = skyColor(dir, uLocalUp, uSunDirection, uAltitude);

    // Sun disk (~0.5 degrees) plus a soft glow; visible from space too.
    float cosToSun = dot(dir, uSunDirection);
    color += vec3(1.0, 0.95, 0.85) * (smoothstep(0.99996, 0.99999, cosToSun) * 20.0 +
                                      pow(max(cosToSun, 0.0), 400.0) * 0.6);

    fragColor = vec4(toDisplay(color), 1.0);
}
