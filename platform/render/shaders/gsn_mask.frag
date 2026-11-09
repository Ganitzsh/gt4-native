#version 460
// N3 step 1: the skip mask (GsnScene's skip mask pass, gsn_light.comp's skipMask): 1 where a skipped item (the lit-road
// layer) would have drawn, by its own depth test against the scene's Z, drawn through gsn_velocity.vert's positions
// (the same to the bit as the scene's). R8; cleared to 0 where nothing was skipped.
layout(location = 0) out vec4 outMask;

void main() { outMask = vec4(1.0); }
