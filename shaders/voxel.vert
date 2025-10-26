#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec2 inTile;   // NEW: atlas tile offset (tx/N, ty/N)

layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec2 vTile;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec2 atlasScale;   // (1/N, 1/N)
} pc;

void main(){
    vNormal = inNormal;
    vUV     = inUV;
    vTile   = inTile;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}