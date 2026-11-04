// Reprojects by the cloud's own depth and the wind; the neighbourhood clamp bounds ghosting
// where that is wrong.
uniform sampler2D uCurrent;
uniform sampler2D uCurrentDepth;
uniform sampler2D uHistory;
uniform sampler2D uHistoryDepth;
uniform mat4 uPreviousViewProjection; // camera-relative, like the main pass
uniform vec3 uCameraDelta;            // current - previous, metres
uniform vec3 uWindDelta;              // metres
uniform vec3 uCameraPlanet;
uniform float uHistoryWeight;         // 0 = no usable history

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outDepth;

// Bilinear resampling every frame would blur the history progressively.
vec4 sampleHistory(vec2 uv, ivec2 size) {
    vec2 texel = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(texel));
    vec2 f = texel - vec2(base);
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w[4] = vec2[](w0, w1, w2, w3);
    vec4 sum = vec4(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            ivec2 q = clamp(base + ivec2(x - 1, y - 1), ivec2(0), size - 1);
            sum += texelFetch(uHistory, q, 0) * (w[x].x * w[y].y);
        }
    }
    return sum;
}

// Distance (m) to the middle of the cloud layer along `ray`, for pixels without cloud this frame.
float layerDistance(vec3 ray) {
    vec3 eye = toAtmosphere(uCameraPlanet);
    vec3 dir = directionToAtmosphere(ray);
    vec2 t = raySphere(eye, dir, kBottomRadius + 0.5 * (kCloudBottom + kCloudTop));
    float km = t.x > 0.0 ? t.x : (t.y > 0.0 ? t.y : 1e6);
    return km / atmosphereKmPerMetre(ray);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uCurrent, 0);
    vec4 current = texelFetch(uCurrent, p, 0);
    vec2 depth = texelFetch(uCurrentDepth, p, 0).xy;
    outColor = current;
    outDepth = depth;
    if (uHistoryWeight <= 0.0) {
        return;
    }

    vec4 lo = current;
    vec4 hi = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec4 c = texelFetch(uCurrent, clamp(p + ivec2(x, y), ivec2(0), size - 1), 0);
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }

    vec3 ray = viewRay(vec2(p) * 2.0 + 1.0);
    float distance = depth.x > 0.0 ? depth.x : layerDistance(ray);
    vec4 clip = uPreviousViewProjection * vec4(ray * distance + uCameraDelta - uWindDelta, 1.0);
    if (clip.w <= 0.0) {
        return;
    }
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return;
    }
    vec4 history = clamp(sampleHistory(uv, size), lo, hi);
    float historyDepth = texelFetch(uHistoryDepth, ivec2(uv * vec2(size)), 0).x;

    outColor = mix(current, history, uHistoryWeight);
    if (depth.x < 0.0) {
        outDepth.x = historyDepth;
    } else if (historyDepth > 0.0) {
        outDepth.x = mix(depth.x, historyDepth, uHistoryWeight);
    }
}
