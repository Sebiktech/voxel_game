#version 450
layout(location=0) in vec3  inPos;
layout(location=1) in vec3  inNormal;
layout(location=2) in vec2  inUV;        // 0..1 per face
layout(location=3) in uvec2 inTile;      // integer tile coords
layout(location=4) in float inAO;        // 0..1, baked by mesher

layout(location=0) out vec2  vUV;
layout(location=1) out vec3  vN;
layout(location=2) out float vAO;

layout(push_constant) uniform Push {
    mat4 uMVP;           // 64 B
    vec2 uAtlasScale;    // 1/ATLAS_N
    vec2 uAtlasTexel;    // 1/(atlasW,H)
} pc;

void main() {
    gl_Position = pc.uMVP * vec4(inPos, 1.0);
    vN  = inNormal;
    vAO = clamp(inAO, 0.0, 1.0);

    // clamp in local tile space using texel converted to local space:
    vec2 epsLocal = pc.uAtlasTexel / pc.uAtlasScale;
    vec2 uvLocal  = clamp(inUV, epsLocal, 1.0 - epsLocal);
    vUV = (vec2(inTile) + uvLocal) * pc.uAtlasScale;
}