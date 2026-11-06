#version 450 core

// Camera-relative: no absolute ECEF coordinate reaches the GPU.

layout(location = 0) in vec3 aPosition;    // relative to the chunk origin
layout(location = 1) in vec3 aMorphDelta;
layout(location = 2) in vec4 aNormal;
layout(location = 3) in vec4 aMorphNormal;
layout(location = 4) in vec2 aElevation;   // own, parent

uniform mat4 uViewProjection; // rotation-only view
uniform vec3 uChunkOffset;    // chunk origin - camera, subtracted in double
uniform vec3 uLodOffset;      // chunk origin - LOD camera (differs when frozen)
uniform vec2 uMorphRange;
uniform int uMorphEnabled;

out VsOut {
    vec3 cameraRelative;
    vec3 normal;
    float elevation;
} vsOut;

void main() {
    // Depends only on the vertex position, so chunks sharing a border vertex morph it alike.
    float distanceToLodCamera = length(aPosition + uLodOffset);
    float morph = uMorphEnabled != 0
                      ? smoothstep(uMorphRange.x, uMorphRange.y, distanceToLodCamera)
                      : 0.0;

    vec3 cameraRelative = aPosition + aMorphDelta * morph + uChunkOffset;
    vsOut.cameraRelative = cameraRelative;
    vsOut.normal = normalize(mix(aNormal.xyz, aMorphNormal.xyz, morph));
    vsOut.elevation = mix(aElevation.x, aElevation.y, morph);
    gl_Position = uViewProjection * vec4(cameraRelative, 1.0);
}
