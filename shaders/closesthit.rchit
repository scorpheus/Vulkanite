#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

struct RayPayload {
	vec3 color;
	float distance;
	vec3 normal;
	float reflector;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;

hitAttributeEXT vec2 attribs;

struct Vertex{
  vec3 pos;
  vec3 normal;
  vec4 tangent;  
  vec3 color;
  vec2 uv0;
  vec2 uv1;
};

struct OffsetPrim{
  uint offsetVertex;
  uint offsetIndex; 
};

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 0) uniform UBO 
{
	mat4 viewInverse;
	mat4 projInverse;
	vec4 lightPos;
	int vertexSize;
} ubo;
layout(binding = 3, set = 0) buffer Vertices {Vertex v[]; } vertices;
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices;
layout(binding = 5, set = 0) buffer OffsetPrims { OffsetPrim v[]; } offsetPrims;

void main()
{
	Vertex v0 = vertices.v[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetVertex + indices.i[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetIndex + 3 * gl_PrimitiveID]];
	Vertex v1 = vertices.v[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetVertex + indices.i[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetIndex + 3 * gl_PrimitiveID + 1]];
	Vertex v2 = vertices.v[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetVertex + indices.i[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetIndex + 3 * gl_PrimitiveID + 2]];

	// Interpolate normal
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
	normal = normalize(vec3(normal * gl_WorldToObjectEXT));
	vec3 vColor = normalize(v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z);

	// Basic lighting
	vec3 lightVector = normalize(ubo.lightPos.xyz);
	float dot_product = max(dot(lightVector, normal), 0.6);
	rayPayload.color = vec3(0.9,0.9,0.9);//vColor.rgb * vec3(dot_product);
	rayPayload.distance = gl_RayTmaxEXT;
	rayPayload.normal = normal;

	// Objects with full white vertex color are treated as reflectors
	rayPayload.reflector = 1.0f;// ((v0.color.r == 1.0f) && (v0.color.g == 1.0f) && (v0.color.b == 1.0f)) ? 1.0f : 0.0f; 
}
