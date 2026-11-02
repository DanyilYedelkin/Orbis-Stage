#version 450 core

// Camera-relative rendering: the camera sits at the origin of the coordinate system the GPU sees.
// No vertex ever carries an absolute planetary coordinate (~6.4e6 m), so float precision is
// spent where the viewer is looking instead of on the distance to the Earth's centre.

layout(location = 0) in vec3 aPosition;   // metres, relative to the chunk origin
layout(location = 1) in vec3 aNormal;     // unit, ECEF-aligned
layout(location = 2) in float aElevation; // metres, negative = below sea level

uniform mat4 uViewProjection; // projection * rotation-only view
uniform vec3 uChunkOffset;    // chunk origin - camera position (subtracted in double on the CPU)

out VsOut {
    vec3 cameraRelative;
    vec3 normal;
    float elevation;
} vsOut;

void main() {
    vec3 cameraRelative = aPosition + uChunkOffset;
    vsOut.cameraRelative = cameraRelative;
    vsOut.normal = aNormal;
    vsOut.elevation = aElevation;
    gl_Position = uViewProjection * vec4(cameraRelative, 1.0);
}
