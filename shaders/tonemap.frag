// Final pass: linear HDR scene colour -> display (ACES filmic + sRGB-ish gamma, common.glsl).
uniform sampler2D uSceneColor;

out vec4 fragColor;

void main() {
    vec3 hdr = texelFetch(uSceneColor, ivec2(gl_FragCoord.xy), 0).rgb;
    fragColor = vec4(toDisplay(hdr), 1.0);
}
