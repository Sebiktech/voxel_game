#version 450

layout(location=0) in vec3 vNormal;  // flat per-face normal from mesher
layout(location=1) in vec2 vUV;      // unwrapped (we'll wrap without killing derivatives)
layout(location=2) in vec2 vTile;    // bottom-left of the tile in atlas UV

layout(set=0, binding=0) uniform sampler2D uAtlas;

// --- Push constants (keep layout identical to your VS) ---
layout(push_constant) uniform Push {
    mat4 mvp_unused;   // matches VS layout
    vec2 atlasScale;   // size of one tile (1/N, 1/N)
    vec2 atlasTexel; // ONLY present if you're on the 80B path
} pc;

layout(location=0) out vec4 outColor;

void main() {
    // --- atlas UV (seam-free) ---
    vec2 uvLocal = vUV - floor(vUV);  // wrap without breaking derivatives

    // Pick ONE padding line matching your push-constants size:
    // A) 72B path (no atlasTexel):
    //vec2 pad = max(pc.atlasScale * 0.02, vec2(1e-6)); // ~2% inset
    // B) 80B path (uncomment AND ensure pc block has atlasTexel):
    vec2 pad = max(pc.atlasTexel * 0.5, vec2(1e-6)); // half-texel inset

    vec2 tileMin = vTile + pad;
    vec2 tileMax = vTile + pc.atlasScale - pad;
    vec2 uvAtlas = mix(tileMin, tileMax, uvLocal);

    // Correct derivatives in atlas space to fix mips at wraps
    vec2 scale = (tileMax - tileMin);
    vec2 dudx  = dFdx(vUV) * scale;
    vec2 dudy  = dFdy(vUV) * scale;

    vec4 albedo = textureGrad(uAtlas, uvAtlas, dudx, dudy);

    // --- lighting ---
    // normalized sun direction (can tweak live)
    const vec3 sunDir   = normalize(vec3(0.35, 0.80, 0.45));
    const vec3 sunColor = vec3(1.0);

    // face normal (already flat per triangle from your mesher)
    vec3 N = normalize(vNormal);

    // ambient + lambert
    float ambient  = 0.35;                              // base ambient
    float NdotL    = max(dot(N, sunDir), 0.0);          // lambert
    float diffuse  = NdotL * 0.85;                      // sun intensity

    // per-face tint: top brightest, sides mid, bottom darker (voxel look)
    float faceTint = (N.y >  0.5) ? 1.00 :              // top
                     (N.y < -0.5) ? 0.65 :              // bottom
                                    0.80;               // sides

    float light = (ambient * faceTint) + diffuse;
    vec3 lit   = albedo.rgb * sunColor * light;

    // (optional) simple gamma to avoid dulling—uncomment if swapchain is UNorm (not sRGB)
    //lit = pow(lit, vec3(1.0/2.2));

    outColor = vec4(lit, albedo.a);
}
