// Analytic sky of the test scene: a Rayleigh-like gradient, a Mie glow around the sun (Henyey-Greenstein
// phase function) and an HDR sun disc, over a dark ground plane. Shared by the sky pass and the
// environment map generation so reflections match the visible sky.
#ifndef GT4_SKY_GLSL
#define GT4_SKY_GLSL

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

vec3 skyRadiance(vec3 direction, vec3 sun, float sunScale, bool withSunDisc) {
    float up = direction.y;
    vec3 zenith = vec3(0.18, 0.36, 0.85);
    vec3 horizon = vec3(0.70, 0.80, 0.95);
    vec3 ground = vec3(0.10, 0.09, 0.08);
    float t = pow(1.0 - clamp(up, 0.0, 1.0), 4.0);
    vec3 sky = mix(zenith, horizon, t);
    // Below the horizon the ground (lit grass/asphalt seen from far) fades in quickly.
    sky = mix(sky, ground, smoothstep(0.0, -0.08, up));
    float cosSun = dot(direction, sun);
    vec3 mie = vec3(1.0, 0.85, 0.6) * henyeyGreenstein(cosSun, 0.76) * 0.6;
    vec3 radiance = (sky + mie * step(-0.02, up)) * sunScale;
    if (withSunDisc) {
        // Sun disc of ~0.53 degrees with a little limb darkening; very bright so it blooms.
        float disc = smoothstep(0.999955, 0.999975, cosSun);
        radiance += vec3(1.0, 0.92, 0.80) * disc * 4000.0 * sunScale;
    }
    return radiance;
}

#endif
