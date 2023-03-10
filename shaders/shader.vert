#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord0;
layout(location = 5) in vec2 inTexCoord1;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord0;
layout(location = 2) out vec2 fragTexCoord1;
layout(location = 3) out vec3 fragNorm;
layout(location = 4) out vec3 fragWorldPos;
layout(location = 5) out vec4 fragTangent;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 invView;
    mat4 proj;
} ubo;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = (ubo.model * vec4(inPosition, 1.0)).xyz;
    fragColor = inColor;
    fragTexCoord0 = inTexCoord0;
    fragTexCoord1 = inTexCoord1;
    fragNorm = (ubo.model * vec4(inNorm, 0)).xyz;
    fragTangent = vec4((ubo.model * vec4(inTangent.xyz, 0)).xyz, inTangent.w);
}