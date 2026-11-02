#version 450 core

// Fullscreen triangle from gl_VertexID alone: no vertex buffer at all. One triangle that covers
// the screen avoids the diagonal seam (and the duplicated pixel shading) of a two-triangle quad.
out vec2 vNdc;

void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0; // (-1,-1) (3,-1) (-1,3)
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
