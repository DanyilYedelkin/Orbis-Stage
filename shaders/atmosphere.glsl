// Hillaire 2020. Constants are generated from AtmosphereParameters. Units: km, 1/km.

uniform sampler2D uTransmittanceLut;
uniform sampler2D uMultiScatteringLut;
uniform sampler2D uIrradianceLut;

const float kPi = 3.14159265358979;

struct Medium {
    vec3 rayleigh;   // scattering
    float mie;       // scattering
    vec3 extinction; // scattering + absorption
};

Medium mediumAt(float altitude) {
    float rayleighDensity = exp(-altitude / kRayleighScaleHeight);
    float mieDensity = exp(-altitude / kMieScaleHeight);
    float ozoneDensity = max(0.0, 1.0 - abs(altitude - kOzoneCenter) / (kOzoneWidth * 0.5));
    Medium m;
    m.rayleigh = kRayleighScattering * rayleighDensity;
    m.mie = kMieScattering * mieDensity;
    m.extinction = m.rayleigh + vec3(kMieExtinction * mieDensity) + kOzoneAbsorption * ozoneDensity;
    return m;
}

// Texel centres on x = 0 and 1: horizon and zenith are stored exactly.
float unitToTexCoord(float x, float size) { return 0.5 / size + x * (1.0 - 1.0 / size); }

float distanceToTop(float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + kTopRadius * kTopRadius;
    return max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
}

// Twin of transmittanceUnitToRMu / transmittanceRMuToUnit.
void transmittanceUnitToRMu(vec2 unit, out float r, out float mu) {
    float h = sqrt(kTopRadius * kTopRadius - kBottomRadius * kBottomRadius);
    float rho = h * unit.y;
    r = sqrt(rho * rho + kBottomRadius * kBottomRadius);
    float dMin = kTopRadius - r;
    float dMax = rho + h;
    float d = dMin + unit.x * (dMax - dMin);
    mu = d == 0.0 ? 1.0 : clamp((h * h - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}

vec2 transmittanceRMuToUv(float r, float mu) {
    float h = sqrt(kTopRadius * kTopRadius - kBottomRadius * kBottomRadius);
    float rho = sqrt(max(r * r - kBottomRadius * kBottomRadius, 0.0));
    float dMin = kTopRadius - r;
    float dMax = rho + h;
    float xMu = (distanceToTop(r, mu) - dMin) / (dMax - dMin);
    return vec2(unitToTexCoord(xMu, kTransmittanceSize.x), unitToTexCoord(rho / h, kTransmittanceSize.y));
}

// The planet's shadow is softened over the sun's disk.
vec3 sunTransmittance(float r, float muSun) {
    float sinHorizon = kBottomRadius / r;
    float cosHorizon = -sqrt(max(1.0 - sinHorizon * sinHorizon, 0.0));
    float visible = smoothstep(-0.0047, 0.0047, muSun - cosHorizon);
    return texture(uTransmittanceLut, transmittanceRMuToUv(r, muSun)).rgb * visible;
}

vec3 multipleScattering(float r, float muSun) {
    vec2 uv = vec2(unitToTexCoord(muSun * 0.5 + 0.5, kMultiScatteringSize),
                   unitToTexCoord(clamp((r - kBottomRadius) / (kTopRadius - kBottomRadius), 0.0, 1.0),
                                  kMultiScatteringSize));
    return texture(uMultiScatteringLut, uv).rgb;
}

// On a horizontal surface, sun illuminance 1, no direct light.
vec3 skyIrradiance(float r, float muSun) {
    vec2 uv = vec2(unitToTexCoord(muSun * 0.5 + 0.5, kIrradianceSize.x),
                   unitToTexCoord(clamp((r - kBottomRadius - 0.001) / (kTopRadius - kBottomRadius - 0.002),
                                        0.0, 1.0), kIrradianceSize.y));
    return texture(uIrradianceLut, uv).rgb;
}

float rayleighPhase(float c) { return 3.0 / (16.0 * kPi) * (1.0 + c * c); }

float miePhase(float c) { // Cornette-Shanks
    float g = kMieAnisotropy;
    float g2 = g * g;
    float k = 3.0 / (8.0 * kPi) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / pow(1.0 + g2 - 2.0 * g * c, 1.5);
}

// (near, far), both negative on a miss.
vec2 raySphere(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return vec2(-1.0);
    }
    float s = sqrt(discriminant);
    return vec2(-b - s, -b + s);
}

// Sun illuminance 1. Steps are denser near t0; each is integrated analytically, which conserves
// energy even with few steps.
void integrateScattering(vec3 origin, vec3 dir, float t0, float t1, vec3 sunDir, int steps,
                         out vec3 luminance, out vec3 transmittance) {
    luminance = vec3(0.0);
    transmittance = vec3(1.0);
    float cosTheta = dot(dir, sunDir);
    float phaseR = rayleighPhase(cosTheta);
    float phaseM = miePhase(cosTheta);
    float previous = t0;
    for (int i = 0; i < steps; ++i) {
        float x = (float(i) + 1.0) / float(steps);
        float t = t0 + (t1 - t0) * x * x;
        float dt = t - previous;
        vec3 p = origin + dir * (0.5 * (t + previous));
        previous = t;

        float r = length(p);
        float muSun = dot(p / r, sunDir);
        Medium m = mediumAt(r - kBottomRadius);
        vec3 stepTransmittance = exp(-m.extinction * dt);
        vec3 scattering = (m.rayleigh * phaseR + m.mie * phaseM) * sunTransmittance(r, muSun) +
                          (m.rayleigh + m.mie) * multipleScattering(r, muSun);
        vec3 integral = (scattering - scattering * stepTransmittance) / max(m.extinction, vec3(1e-9));
        luminance += transmittance * integral;
        transmittance *= stepTransmittance;
    }
}

// ECEF metres -> km in the spherical model. Scaling z by a/b maps the ellipsoid to the sphere,
// heights to within 0.3%.
vec3 toAtmosphere(vec3 planetMeters) {
    return vec3(planetMeters.xy, planetMeters.z * kEllipsoidAOverB) * kMetersToAtmosphereKm;
}
vec3 directionToAtmosphere(vec3 d) { return normalize(vec3(d.xy, d.z * kEllipsoidAOverB)); }
