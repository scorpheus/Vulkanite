#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 vPosition;
layout(location = 1) out vec4 vPrevPosition;

layout(binding = 0) uniform UniformBufferObject {
    mat4 uModelViewProjectionMat;
    mat4 uPrevModelViewProjectionMat;
} ubo;

void main() {
    vPosition = ubo.uModelViewProjectionMat * vec4(inPosition, 1.0);
    vPrevPosition = ubo.uPrevModelViewProjectionMat * vec4(inPosition, 1.0);

    gl_Position = vPosition;
}