// Volumetric clouds, composited over the HDR scene (sky + terrain) with
// glBlendFunc(GL_ONE, GL_SRC_ALPHA):  result = rgb + scene * a.
//
// For each pixel the view ray is marched through the cloud shell (up to two intervals), stopping
// at the terrain (scene depth). Every sample is lit by the sun - through the atmosphere
// (transmittance LUT) and through the cloud above it (a short light march) - with a two-lobe
// phase function and an approximation of multiple scattering (octaves of ever weaker absorption
// and ever more isotropic phase, after Wrenninge / Hillaire), plus ambient light from the sky
// irradiance table.
//
// Aerial perspective is applied EXACTLY for a cloud at one effective depth d (the
// transmittance-weighted mean depth of the cloud along the ray). The scene behind already
// contains the air's in-scattering S(0, D) along the whole ray; with the cloud's transmittance
// Tc and luminance Lc, the correct result S(0, d) + T(0, d) (Lc + Tc (S(d, D) + ...)) equals
//     scene * Tc  +  [ T(0, d) Lc + (1 - Tc) S(0, d) ],
// which is exactly what the blend computes.
in vec2 vNdc;

uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTanHalfFov;
uniform vec3 uCameraPlanet;  // metres, ECEF relative to the planet centre
uniform vec3 uSunDirection;  // unit, ECEF
uniform float uSunIlluminance;
uniform float uNearPlane;    // metres: reversed-Z infinite projection, depth = near / viewDepth
uniform float uPixelAngle;   // radians per pixel
uniform sampler2D uSceneDepth;

out vec4 fragColor;

// A per-pixel random offset of the first sample turns banding from the finite step count into
// fine grain. (Interleaved gradient noise was tried first: its regular structure showed as a
// cross-hatch along cloud edges.)
float pixelNoise(vec2 pixel) {
    vec3 p = fract(vec3(pixel.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

const int kViewSteps = 64;
const float kCloudGroundAlbedo = 0.15;
const int kLightSteps = 5;

void main() {
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    if (uCloudsEnabled == 0) {
        return;
    }
    vec3 dirWorld = normalize(uForward + vNdc.x * uTanHalfFov.x * uRight + vNdc.y * uTanHalfFov.y * uUp);
    vec3 eye = toAtmosphere(uCameraPlanet);
    vec3 dir = directionToAtmosphere(dirWorld);
    vec3 sun = directionToAtmosphere(uSunDirection);

    // Where the terrain stops the ray, in atmosphere km along `dir`.
    float depth = texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    float maxT = 1e9;
    if (depth > 0.0) {
        float metres = (uNearPlane / depth) / dot(dirWorld, uForward);
        maxT = metres * length(vec3(dirWorld.xy, dirWorld.z * kEllipsoidAOverB)) * kMetersToAtmosphereKm;
    }

    vec2 segments[2];
    int count = cloudIntervals(eye, dir, segments[0], segments[1]);
    if (count == 0) {
        return;
    }
    // Distant cloud near the horizon is mostly haze; cap the marched length per interval.
    float total = 0.0;
    for (int k = 0; k < count; ++k) {
        segments[k].y = min(min(segments[k].y, maxT), segments[k].x + 250.0);
        total += max(segments[k].y - segments[k].x, 0.0);
    }
    if (total <= 0.0) {
        return;
    }

    float cosView = dot(dir, sun);
    float jitter = pixelNoise(gl_FragCoord.xy);
    vec3 luminance = vec3(0.0);
    float transmittance = 1.0;
    float depthSum = 0.0;
    float weightSum = 0.0;
    float dt = total / float(kViewSteps);

    for (int k = 0; k < count; ++k) {
        float t0 = segments[k].x;
        float t1 = segments[k].y;
        if (t1 <= t0) {
            continue;
        }
        int steps = max(int(ceil((t1 - t0) / dt)), 1);
        float step = (t1 - t0) / float(steps);
        for (int s = 0; s < steps && transmittance > 0.01; ++s) {
            float t = t0 + (float(s) + jitter) * step;
            vec3 p = eye + dir * t;
            vec3 q = p - eye + uCloudOrigin;
            // Detail erosion only where it can be resolved.
            float detail = 1.0 - smoothstep(8.0, 40.0, t);
            float footprint = t * uPixelAngle;
            float density = cloudDensity(p, q, detail, footprint);
            if (density <= 0.0) {
                continue;
            }
            float sigma = density * kCloudExtinction;
            float r = length(p);
            float muSun = dot(p / r, sun);

            // Light march towards the sun: optical depth of the cloud above this sample.
            float lightDepth = 0.0;
            float lt = 0.0;
            float ls = 0.12;
            for (int l = 0; l < kLightSteps; ++l) {
                vec3 lp = p + sun * (lt + 0.5 * ls);
                lightDepth += cloudDensity(lp, lp - eye + uCloudOrigin, 0.0, footprint) * ls;
                lt += ls;
                ls *= 1.9;
            }
            lightDepth *= kCloudExtinction;

            // Multiple scattering octaves: light that scattered several times is attenuated
            // less (a), travels "further" (b) and has a more isotropic phase (c).
            float sunScattering = 0.0;
            float a = 1.0;
            float b = 1.0;
            float c = 1.0;
            for (int o = 0; o < 3; ++o) {
                sunScattering += a * exp(-b * lightDepth) * cloudPhase(cosView, c);
                a *= 0.5;
                b *= 0.4;
                c *= 0.5;
            }
            // Powder effect: the sunlit fringe of a dense cloud is darker seen from the sun's side.
            sunScattering *= mix(1.0, 1.0 - exp(-2.0 * lightDepth), 0.5 * (1.0 - 0.5 * (cosView + 1.0)));

            float h = (r - kBottomRadius - kCloudBottom) / (kCloudTop - kCloudBottom);
            // Ambient: the sky from above (less of it reaches into the base), and the sunlit
            // ground from below - what keeps cumulus bases grey rather than navy.
            vec3 sky = skyIrradiance(r, muSun) / kPi * mix(0.5, 1.0, h) +
                       kCloudGroundAlbedo / kPi * sunTransmittance(r, muSun) * max(muSun, 0.0) * (1.0 - h);
            vec3 source = sigma * (sunTransmittance(r, muSun) * sunScattering + sky);

            float stepTransmittance = exp(-sigma * step);
            float absorbed = transmittance * (1.0 - stepTransmittance);
            luminance += transmittance * source * (1.0 - stepTransmittance) / sigma;
            depthSum += absorbed * t;
            weightSum += absorbed;
            transmittance *= stepTransmittance;
        }
    }
    if (weightSum <= 0.0) {
        return;
    }

    // Aerial perspective in front of the cloud (see the header comment).
    float cloudDepth = depthSum / weightSum;
    vec3 airLight = vec3(0.0);
    vec3 airTransmittance = vec3(1.0);
    vec2 top = raySphere(eye, dir, kTopRadius);
    float a0 = max(top.x, 0.0);
    if (cloudDepth > a0) {
        integrateScattering(eye, dir, a0, cloudDepth, sun, 10, airLight, airTransmittance);
    }
    vec3 color = (airTransmittance * luminance + (1.0 - transmittance) * airLight) * uSunIlluminance;
    fragColor = vec4(color, transmittance);
}
