#version 460
#extension GL_GOOGLE_include_directive : require
#include "scene_common.glsl"
layout(location = 0) in vec3 inPosition;
void main() {
    gl_Position = frame.lightViewProjection * (instances[gl_InstanceIndex].model * vec4(inPosition, 1.0));
}
