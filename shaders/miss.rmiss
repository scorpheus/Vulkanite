#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
	vec3 color;
	float depth;
	int currentRecursion;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;
layout(binding = 8, set = 0) uniform sampler2D envMap;

const float PI = 3.141592653589793;

void main()
{
//	// View-independent background gradient to simulate a basic sky background
//	const vec3 gradientStart = vec3(0.5, 0.6, 1.0);
//	const vec3 gradientEnd = vec3(1.0);
//	vec3 unitDir = normalize(gl_WorldRayDirectionEXT);
//	float t = 0.5 * (unitDir.y + 1.0);
//	rayPayload.color = (1.0-t) * gradientStart + t * gradientEnd;
	//

	vec3 dir = normalize(gl_WorldRayDirectionEXT);
	float theta = atan(dir.z, dir.x);
	float phi = acos(dir.y);

	// Compute the texture coordinates from the spherical coordinates
	vec2 uv = vec2(theta / (2.0 * PI), phi / PI);
	rayPayload.color = texture(envMap, uv).xyz;
}