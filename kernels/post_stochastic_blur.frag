#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D   u_tex0;
uniform vec2        u_resolution;
uniform float       u_time;

#ifdef DOUBLE_BUFFER_0
uniform sampler2D   u_doubleBuffer0;
#endif

// Tunables
#ifndef STOCHASTICBLUR_SOURCE
#define STOCHASTICBLUR_SOURCE u_tex0
#endif

#ifndef STOCHASTICBLUR_RADIUS
#define STOCHASTICBLUR_RADIUS 3.0
#endif

// Only used when DOUBLE_BUFFER_0 is enabled.
// 0.0 = no history, 0.95 = very stable but slower response.
#ifndef STOCHASTICBLUR_HISTORY
#define STOCHASTICBLUR_HISTORY 0.92
#endif

// Self-contained helpers (no external includes)
#ifndef TAU
#define TAU 6.28318530718
#endif

// Hash-like random for vec3 -> vec2 in [0,1]
vec2 random2(vec3 p) {
    const vec4 RANDOM_SCALE = vec4(443.897, 441.423, 0.0973, 0.1099);
    p = fract(p * RANDOM_SCALE.xyz);
    p += dot(p, p.yzx + 19.19);
    return fract((p.xx + p.yz) * p.zy);
}

vec4 sampleClamp2edge(sampler2D tex, vec2 uv) {
    return texture2D(tex, clamp(uv, vec2(0.0), vec2(1.0)));
}

vec3 stochasticBlur(sampler2D tex, vec2 st, vec2 pixel, float radiusPx) {
    vec2 rnd = random2(vec3(st * u_resolution.xy, u_time * 0.173));
    float a = TAU * rnd.x;
    float r = sqrt(rnd.y) * radiusPx;
    vec2 offset = vec2(cos(a), sin(a)) * r * pixel;
    return sampleClamp2edge(tex, st + offset).rgb;
}

void main(void) {
    vec2 pixel = 1.0 / u_resolution;
    vec2 st = gl_FragCoord.xy * pixel;

    vec3 color = stochasticBlur(STOCHASTICBLUR_SOURCE, st, pixel, STOCHASTICBLUR_RADIUS);

#ifdef DOUBLE_BUFFER_0
    vec3 history = texture2D(u_doubleBuffer0, st).rgb;
    color = mix(color, history, STOCHASTICBLUR_HISTORY);
#endif

    gl_FragColor = vec4(color, 1.0);
}
