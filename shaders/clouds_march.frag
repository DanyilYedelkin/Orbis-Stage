// Aerial perspective is exact for a cloud at one effective depth d. The scene already holds
// S(0, D) for the whole ray, so S(0, d) + T(0, d) (Lc + Tc (S(d, D) + ...)) reduces to
//   scene * Tc + T(0, d) Lc + (1 - Tc) S(0, d),
// the composite's ONE / SRC_ALPHA blend of this pass's output.
uniform vec3 uCameraPlanet; // metres, ECEF from the planet centre
uniform vec3 uSunDirection;
uniform float uSunIlluminance;
uniform float uPixelAngle;  // radians per full-resolution pixel
uniform ivec2 uSubPixel;
uniform float uFrame;
uniform sampler2D uSceneDepth;

layout(location = 0) out vec4 outColor; // rgb: added light, a: transmittance
layout(location = 1) out vec2 outDepth; // x: cloud depth (m, -1 = none), y: scene distance (m)

// Interleaved gradient noise was tried first; its regular structure showed as a cross-hatch
// along cloud edges.
float pixelNoise(vec2 pixel) {
    vec3 p = fract(vec3(pixel.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

const int kViewSteps = 48;
const float kCloudGroundAlbedo = 0.15;
const int kLightSteps = 5;

void main() {
    ivec2 pixel = min(ivec2(gl_FragCoord.xy) * 2 + uSubPixel, ivec2(uFullSize) - 1);
    vec3 dirWorld = viewRay(vec2(pixel) + 0.5);
    float surface = sceneDistance(texelFetch(uSceneDepth, pixel, 0).r, dirWorld);
    float kmPerMetre = atmosphereKmPerMetre(dirWorld);
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
    outDepth = vec2(-1.0, surface);

    if (uCloudsEnabled == 0) {
        return;
    }
    vec3 eye = toAtmosphere(uCameraPlanet);
    vec3 dir = directionToAtmosphere(dirWorld);
    vec3 sun = directionToAtmosphere(uSunDirection);
    float maxT = surface * kmPerMetre;

    vec2 segments[2];
    int count = cloudIntervals(eye, dir, segments[0], segments[1]);
    if (count == 0) {
        return;
    }
    // Beyond ~250 km a cloud is mostly haze.
    float total = 0.0;
    for (int k = 0; k < count; ++k) {
        segments[k].y = min(min(segments[k].y, maxT), segments[k].x + 250.0);
        total += max(segments[k].y - segments[k].x, 0.0);
    }
    if (total <= 0.0) {
        return;
    }

    float cosView = dot(dir, sun);
    float jitter = fract(pixelNoise(gl_FragCoord.xy) + uFrame * 0.618034);
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
            float detail = 1.0 - smoothstep(8.0, 40.0, t);
            float footprint = t * uPixelAngle;
            float density = cloudDensity(p, q, detail, footprint);
            if (density <= 0.0) {
                continue;
            }
            float sigma = density * kCloudExtinction;
            float r = length(p);
            float muSun = dot(p / r, sun);

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

            // Multiple scattering octaves (Wrenninge): weaker extinction (b), more isotropic
            // phase (c), lower weight (a).
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
            // Powder: dense cloud edges seen from the sun's side are darker.
            sunScattering *= mix(1.0, 1.0 - exp(-2.0 * lightDepth), 0.5 * (1.0 - 0.5 * (cosView + 1.0)));

            float h = (r - kBottomRadius - kCloudBottom) / (kCloudTop - kCloudBottom);
            // Ambient: sky from above, fading into the base; sunlit ground from below, which is
            // what keeps cumulus bases grey rather than navy.
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

    float cloudDepth = depthSum / weightSum;
    vec3 airLight = vec3(0.0);
    vec3 airTransmittance = vec3(1.0);
    vec2 top = raySphere(eye, dir, kTopRadius);
    float a0 = max(top.x, 0.0);
    if (cloudDepth > a0) {
        integrateScattering(eye, dir, a0, cloudDepth, sun, 10, airLight, airTransmittance);
    }
    outColor = vec4((airTransmittance * luminance + (1.0 - transmittance) * airLight) * uSunIlluminance,
                    transmittance);
    outDepth.x = cloudDepth / kmPerMetre;
}
