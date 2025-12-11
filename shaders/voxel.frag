#version 450
layout(set=0, binding=0) uniform sampler2D uAtlas;
layout(set=0, binding=2) uniform Lighting {
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
} L;

layout(location=0) in vec2  vUV;
layout(location=1) in vec3  vN;
layout(location=2) in float vAO;

layout(location=0) out vec4 outColor;

void main() {
    vec3 albedo = texture(uAtlas, vUV).rgb;
    vec3 N  = normalize(vN);
    vec3 Ld = normalize(-L.sunDir.xyz);
    float ndl = max(dot(N, Ld), 0.0);
    vec3 light = L.ambient.rgb + L.sunColor.rgb * ndl;

    float ao = clamp(vAO, 0.0, 1.0);
    light = mix(light * 0.6, light, ao);

    outColor = vec4(albedo * light, 1.0);
}