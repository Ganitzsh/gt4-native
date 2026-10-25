#ifndef GT4_DOF_GLSL
#define GT4_DOF_GLSL
// Thin-lens circle of confusion in pixels (signed: negative in front of the focus plane).
//   coc = A f (z - s) / (z (s - f)), scaled from the sensor height to the image height.
struct DofParams {
    float near;            // camera near plane (reverse-Z infinite projection: z = near / depth)
    float focusDistance;   // metres; <= 0 means autofocus (passed resolved from the CPU or the centre pixel)
    float focalLength;     // metres
    float aperture;        // lens diameter in metres (focal length / f-number)
    float sensorHeight;    // metres
    float imageHeight;     // pixels of the target the CoC is measured in
    float maxCoc;          // pixels
};
float linearDepth(float depth, float near) { return near / max(depth, 1e-7); }
float circleOfConfusion(float z, DofParams p) {
    float s = p.focusDistance;
    float coc = p.aperture * p.focalLength * (z - s) / (z * max(s - p.focalLength, 1e-4));
    return clamp(coc / p.sensorHeight * p.imageHeight, -p.maxCoc, p.maxCoc);
}
#endif
