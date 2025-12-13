#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;      // 0..1 per face
layout(location=3) in vec2 inTile;    // tile origin in atlas [0..1]
layout(location=4) in float inAO;     // ambient occlusion (0..1)

layout(location=0) out vec2  vUV;
layout(location=1) out vec3  vN;
layout(location=2) out float vAO;

layout(push_constant) uniform Push {
    mat4 uMVP;           // 64B
    vec2 uAtlasScale;    // (1.0/ATLAS_N, 1.0/ATLAS_N)
    vec2 uAtlasTexel;    // (1.0/atlasWidth, 1.0/atlasHeight)
} pc;

void main() {
    gl_Position = pc.uMVP * vec4(inPos, 1.0);
    vN  = inNormal;
    vAO = inAO;

    // Keep samples away from texture borders (to reduce bleeding)
    vec2 eps = pc.uAtlasTexel * 0.5;

    // Clamp local face-UV into [eps, 1-eps]
    vec2 uv = clamp(inUV, eps, 1.0 - eps);

    // inTile is already normalized (tx/ATLAS_N, ty/ATLAS_N),
    // so we only scale the *local* UV by tile size and add the origin.
    vUV = inTile + uv * pc.uAtlasScale;
}