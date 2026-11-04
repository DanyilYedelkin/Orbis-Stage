// Upsamples the resolved clouds to full resolution and blends them over the scene with
// glBlendFunc(GL_ONE, GL_SRC_ALPHA). Bilinear weights are multiplied by how well each
// half-resolution sample's scene distance matches this pixel's, so clouds neither bleed over
// mountain silhouettes nor leave a gap around them.
uniform sampler2D uResolved;
uniform sampler2D uResolvedDepth;
uniform sampler2D uSceneDepth;

out vec4 fragColor;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uResolved, 0);
    float surface = sceneDistance(texelFetch(uSceneDepth, pixel, 0).r, viewRay(vec2(pixel) + 0.5));

    // Half-resolution texel i is centred on full-resolution coordinate 2i + 1.
    vec2 f = (vec2(pixel) + 0.5 - 1.0) * 0.5;
    ivec2 base = ivec2(floor(f));
    vec2 t = f - vec2(base);

    vec4 sum = vec4(0.0);
    float weights = 0.0;
    vec4 closest = vec4(0.0, 0.0, 0.0, 1.0);
    float closestMismatch = 1e30;
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1);
        ivec2 q = clamp(base + o, ivec2(0), size - 1);
        vec4 c = texelFetch(uResolved, q, 0);
        float s = texelFetch(uResolvedDepth, q, 0).y;
        float mismatch = abs(surface - s) / min(surface, s);
        float bilinear = (o.x == 1 ? t.x : 1.0 - t.x) * (o.y == 1 ? t.y : 1.0 - t.y);
        float w = bilinear * exp(-20.0 * mismatch);
        sum += c * w;
        weights += w;
        if (mismatch < closestMismatch) {
            closestMismatch = mismatch;
            closest = c;
        }
    }
    fragColor = weights > 1e-4 ? sum / weights : closest;
}
