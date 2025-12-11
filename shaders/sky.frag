#version 450
layout(location=0) in vec2 vNdc;
layout(location=0) out vec4 outColor;

// Push constants for sky
layout(push_constant) uniform SkyPC {
    vec3 camF;          // camera forward (world)
    float tanHalfFov;   // tan(FOV/2) for vertical FOV
    vec3 camR;          // camera right (world)
    float aspect;       // width/height
    vec3 camU;          // camera up (world)
    float time;         // optional (unused), keep padding nice
    vec3 sunDir;        // direction TOWARDS the sun (world)
    float sunAngle;     // sun angular radius in radians, e.g. 0.009 (~0.5°)
    vec3 sunColor;      // sun light color
    float pad0;
    vec3 skyBase;       // base sky color (horizon)
    float pad1;
} pc;

// Simple Hosek/Nishita-ish fake: horizon->zenith gradient + sun disk + halo.
void main()
{
    // Build world-space view ray from NDC
    // vNdc.xy in [-1,1]
    vec2 xy = vNdc;
    vec3 view =
        normalize(pc.camF +
                  xy.x * pc.aspect * pc.tanHalfFov * pc.camR +
                  xy.y * pc.tanHalfFov * pc.camU);

    // Gradient sky: more blue when looking up
    float t = clamp(view.y * 0.5 + 0.5, 0.0, 1.0);  // 0 at down, 1 at up
    vec3 zenith = vec3(0.18, 0.35, 0.60);           // tweak to taste
    vec3 horizon= pc.skyBase;                       // from PC
    vec3 sky = mix(horizon, zenith, pow(t, 1.2));

    // Sun disk + soft glow
    float cdot = dot(normalize(view), normalize(pc.sunDir));
    float ang  = acos(clamp(cdot, -1.0, 1.0));

    // Disk (hard-ish)
    float disk = smoothstep(pc.sunAngle, pc.sunAngle*0.7, ang); // invert
    disk = 1.0 - disk;

    // Halo/glow
    float glow = exp(- (ang*ang) / (2.0 * (pc.sunAngle*3.0)*(pc.sunAngle*3.0)));

    vec3 color = sky + pc.sunColor * (disk*4.0 + glow*0.6);

    outColor = vec4(color, 1.0);
}
