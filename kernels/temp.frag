#version 450

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float time;
    float _pad;
} pc;

void main() {
    vec2 frag = gl_FragCoord.xy;

    float st = abs(sin(pc.time));

    float gridScale = 128.0;
    float cellSize  = 32.0;

    vec2 uv = (frag / pc.resolution) * gridScale;

    vec2 cell = abs(fract(uv / cellSize) - 0.5) * cellSize;
    float dist = min(cell.x, cell.y);

    float coreWidth = 0.5;
    float core = smoothstep(coreWidth, 0.0, dist);

    float glowStrength = 0.5;
    float glow = exp(-dist * glowStrength);

    vec3 lineColor = vec3(0.7);

    vec3 color = lineColor * (core + glow * 0.6);

    fragColor = vec4(color, 1.0);
}
