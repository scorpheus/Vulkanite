#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

struct RayPayload {
	vec3 color;
	float distance;
	vec3 normal;
	int currentRecursion;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;
layout(location = 2) rayPayloadEXT bool shadowed;

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

// Max. number of recursion is passed via a specialization constant
layout (constant_id = 0) const int MAX_RECURSION = 4;

void main()
{
	Vertex v0 = vertices.v[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetVertex + indices.i[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetIndex + 3 * gl_PrimitiveID]];
	Vertex v1 = vertices.v[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetVertex + indices.i[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetIndex + 3 * gl_PrimitiveID + 1]];
	Vertex v2 = vertices.v[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetVertex + indices.i[offsetPrims.v[gl_InstanceCustomIndexEXT].offsetIndex + 3 * gl_PrimitiveID + 2]];

	// Interpolate values
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	const vec3 position       = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
	const vec3 world_position = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));
	const vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
	const vec3 world_normal = normalize(vec3(normal * gl_WorldToObjectEXT));
	const vec4 tangent = normalize(v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z);
	const vec4 world_tangent = vec4((gl_WorldToObjectEXT * vec4(tangent.xyz, 0)).xyz, tangent.w);
	
	const vec3 vColor = normalize(v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z);
	const vec2 uv0 = normalize(v0.uv0 * barycentricCoords.x + v1.uv0 * barycentricCoords.y + v2.uv0 * barycentricCoords.z);
	const vec2 uv1 = normalize(v0.uv1 * barycentricCoords.x + v1.uv1 * barycentricCoords.y + v2.uv1 * barycentricCoords.z);
    
    
    vec3 lightVector = normalize(ubo.lightPos.xyz);
    float lightDistance  = 100000.0;

    // Lambertian
    float dotNL = max(dot(world_normal, lightVector), 0.0);
    vec3 color = vec3(0.3, 0.7, 0.3) * dotNL;
	
	vec3 origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	float tmin = 0.001;
	float tmax = 10000.0;

	// Shadow casting
	// Tracing shadow ray only if the light is visible from the surface
	if(dot(world_normal, lightVector) > 0) {
		shadowed = true;  
		// Trace shadow ray and offset indices to match shadow hit/miss shader group indices
	
		traceRayEXT(topLevelAS,  // acceleration structure
					gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,       // rayFlags
					0xFF,        // cullMask
					1,           // sbtRecordOffset
					0,           // sbtRecordStride
					1,           // missIndex
					origin,      // ray origin
					tmin,        // ray min range
					lightVector,      // ray direction
					tmax,        // ray max range
					2            // payload (location = 2)
		);
		if (shadowed) {
			color *= 0.3;
		}
	}

	// reflection
	rayPayload.currentRecursion += 1;
	if(rayPayload.currentRecursion < MAX_RECURSION){	
		vec3 direction = reflect(gl_WorldRayDirectionEXT, world_normal);
		
		traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin, tmin, direction, tmax, 0);

		color += rayPayload.color * 0.3;
	}

	// Basic lighting
	rayPayload.color = color; 
	rayPayload.distance = gl_RayTmaxEXT;
	rayPayload.normal = world_normal;

}
