#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    // simple gradient test
    vec2 uv = fract(vUV);
    outColor = vec4(0.2 + 0.8 * uv.x, 0.2 + 0.8 * uv.y, 1.0, 1.0);
}