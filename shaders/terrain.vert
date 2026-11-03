#version 450 core

// Camera-relative rendering: the camera sits at the origin of the coordinate system the GPU sees.
// No vertex ever carries an absolute planetary coordinate (~6.4e6 m), so float precision is
// spent where the viewer is looking instead of on the distance to the Earth's centre.

layout(location = 0) in vec3 aPosition;    // metres, relative to the chunk origin
layout(location = 1) in vec3 aMorphDelta;  // parent-surface position - aPosition
layout(location = 2) in vec4 aNormal;      // unit, ECEF-aligned (unpacked from 10:10:10:2)
layout(location = 3) in vec4 aMorphNormal; // the parent's normal here
layout(location = 4) in vec2 aElevation;   // (own, parent) elevation in metres

uniform mat4 uViewProjection; // projection * rotation-only view
uniform vec3 uChunkOffset;    // chunk origin - camera position (subtracted in double on the CPU)
uniform vec3 uLodOffset;      // chunk origin - LOD camera (= camera unless the LOD is frozen)
uniform vec2 uMorphRange;     // distances where morphing starts / is complete
uniform int uMorphEnabled;

out VsOut {
    vec3 cameraRelative;
    vec3 normal;
    float elevation;
} vsOut;

void main() {
    // Geomorphing. The blend factor depends only on the vertex POSITION (its distance to the LOD
    // camera), never on which chunk it belongs to - so two chunks sharing a border vertex morph
    // it identically and no crack opens between them.
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
