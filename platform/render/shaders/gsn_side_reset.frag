#version 460
// B8 step 2: the side channels cleared where the partition wrote Z (gsn_side_reset.vert).
layout(location = 0) out vec4 outNormal;
layout(location = 1) out uint outMaterial;
layout(location = 2) out float outDepth;
void main() {
    outNormal = vec4(0.0);
    outMaterial = 0u;
    outDepth = 0.0;
}
