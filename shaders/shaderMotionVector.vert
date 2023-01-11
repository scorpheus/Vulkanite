#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord0;
layout(location = 5) in vec2 inTexCoord1;

layout(location = 0) out vec4 vPosition;
layout(location = 1) out vec4 vPrevPosition;

layout(binding = 0) uniform UniformBufferObject {
    mat4 uModelViewProjectionMat;
    mat4 uPrevModelViewProjectionMat;
    mat4 uJitterMat;
} ubo;

void main() {
    vPosition = ubo.uModelViewProjectionMat * ubo.uJitterMat * vec4(inPosition, 1.0);
    vPrevPosition = ubo.uPrevModelViewProjectionMat * ubo.uJitterMat * vec4(inPosition, 1.0);

    gl_Position = vPosition;
}