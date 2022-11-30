#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec2 fragNorm;

// Material bindings

layout(binding = 1) uniform sampler2D colorMap;
layout(binding = 2) uniform sampler2D metallicRoughnessMap;
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2D aoMap;
layout(binding = 5) uniform sampler2D emissiveMap;

layout (push_constant) uniform Material {
	vec4 baseColorFactor;
	vec4 emissiveFactor;
	float workflow;
	int baseColorTextureSet;
	int metallicRoughnessTextureSet;
	int normalTextureSet;	
	int occlusionTextureSet;
	int emissiveTextureSet;
	float metallicFactor;	
	float roughnessFactor;	
	float alphaMask;	
	float alphaMaskCutoff;
} material;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(colorMap, fragTexCoord) *  texture(metallicRoughnessMap, fragTexCoord) *  texture(normalMap, fragTexCoord) *  texture(aoMap, fragTexCoord) *  texture(emissiveMap, fragTexCoord) * material.baseColorFactor;
}