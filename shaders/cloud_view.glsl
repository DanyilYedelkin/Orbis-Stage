// View rays and scene distances for pixels of the full-resolution frame, shared by the cloud
// passes (which run at half resolution and need to address full-resolution pixels).
uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTanHalfFov;
uniform vec2 uFullSize;
uniform float uNearPlane;

const float kNoSurface = 1e12;

vec3 viewRay(vec2 fullPixel) {
    vec2 ndc = fullPixel / uFullSize * 2.0 - 1.0;
    return normalize(uForward + ndc.x * uTanHalfFov.x * uRight + ndc.y * uTanHalfFov.y * uUp);
}

// Metres along `ray` to the surface behind the pixel; reversed-Z infinite: depth = near / z.
float sceneDistance(float depth, vec3 ray) {
    return depth > 0.0 ? (uNearPlane / depth) / dot(ray, uForward) : kNoSurface;
}

// Atmosphere-model km per metre along a world-space ray (the model scales z by a/b).
float atmosphereKmPerMetre(vec3 ray) {
    return length(vec3(ray.xy, ray.z * kEllipsoidAOverB)) * kMetersToAtmosphereKm;
}
