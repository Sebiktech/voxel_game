#version 450
// Full-screen triangle, no vertex buffers needed.
layout(location = 0) out vec2 vUV;

void main() {
    // 3 vertices: ( -1,-1 ), ( 3,-1 ), ( -1, 3 )
    const vec2 verts[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 pos = verts[gl_VertexIndex];
    vUV = 0.5 * pos + 0.5; // map NDC [-1,1] to [0,1]
    gl_Position = vec4(pos, 0.0, 1.0);
}