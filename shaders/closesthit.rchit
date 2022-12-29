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

struct Material{
	int doubleSided;
	int albedoTex;
	int metallicRoughnessTex;
	int aoTex;
	int normalTex;
	int emissiveTex;

	float metallicFactor;	
	float roughnessFactor;	
	float alphaMask;	
	float alphaMaskCutoff;
	int baseColorTextureSet;
	int metallicRoughnessTextureSet;
	int normalTextureSet;	
	int occlusionTextureSet;
	int emissiveTextureSet;
	vec4 baseColorFactor;
	vec3 emissiveFactor;
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
layout(binding = 6, set = 0) uniform sampler2D texturesMap[];
layout(binding = 7, set = 0) buffer MaterialMap {Material v[]; } materialsMap;


// Max. number of recursion is passed via a specialization constant
layout (constant_id = 0) const int MAX_RECURSION = 4;

//
float sRGB2linear(float v) {
	return (v < 0.04045) ? (v * 0.0773993808) : pow((v + 0.055) / 1.055, 2.4);
}

vec3 sRGB2linear(vec3 v) {
	return vec3(sRGB2linear(v.x), sRGB2linear(v.y), sRGB2linear(v.z));
}

void main()
{
	uint matID = (gl_InstanceCustomIndexEXT << 16) >> 16;
	uint offsetID = gl_InstanceCustomIndexEXT >> 16;

	Vertex v0 = vertices.v[offsetPrims.v[offsetID].offsetVertex + indices.i[offsetPrims.v[offsetID].offsetIndex + 3 * gl_PrimitiveID]];
	Vertex v1 = vertices.v[offsetPrims.v[offsetID].offsetVertex + indices.i[offsetPrims.v[offsetID].offsetIndex + 3 * gl_PrimitiveID + 1]];
	Vertex v2 = vertices.v[offsetPrims.v[offsetID].offsetVertex + indices.i[offsetPrims.v[offsetID].offsetIndex + 3 * gl_PrimitiveID + 2]];

	Material mat = materialsMap.v[matID];

	// Interpolate values
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	const vec3 position       = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
	const vec3 world_position = vec3(gl_ObjectToWorldEXT * vec4(position, 1.0));
	const vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
	const vec3 world_normal = normalize(vec3(normal * gl_WorldToObjectEXT));
	const vec4 tangent = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
	const vec4 world_tangent = vec4((gl_WorldToObjectEXT * vec4(tangent.xyz, 0)).xyz, tangent.w);
	
	const vec3 vColor = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;
	const vec2 fragTexCoord0 = v0.uv0 * barycentricCoords.x + v1.uv0 * barycentricCoords.y + v2.uv0 * barycentricCoords.z;
	const vec2 fragTexCoord1 = v0.uv1 * barycentricCoords.x + v1.uv1 * barycentricCoords.y + v2.uv1 * barycentricCoords.z;
    
    
	vec4 albedo_color = texture(texturesMap[mat.albedoTex], mat.baseColorTextureSet == 0 ? fragTexCoord0 : fragTexCoord1 );
	//albedo_color.xyz = albedo_color.xyz * mat.baseColorFactor.xyz;
	albedo_color.xyz = sRGB2linear(albedo_color.xyz) * mat.baseColorFactor.xyz;
	albedo_color.w = albedo_color.w * mat.baseColorFactor.w;

	if(mat.alphaMask == 2 && albedo_color.a < mat.alphaMaskCutoff)
		return;
	 
	float occlusion = texture(texturesMap[mat.aoTex], mat.occlusionTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).r;
	float metalness = texture(texturesMap[mat.metallicRoughnessTex], mat.metallicRoughnessTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).b * mat.metallicFactor;
	float roughness = texture(texturesMap[mat.metallicRoughnessTex], mat.metallicRoughnessTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).g * mat.roughnessFactor;

	
	vec3 emissive = texture(texturesMap[mat.emissiveTex], mat.emissiveTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).xyz * mat.emissiveFactor;


    vec3 lightVector = normalize(ubo.lightPos.xyz);
    float lightDistance  = 100000.0;

    // Lambertian
    float dotNL = max(dot(world_normal, lightVector), 0.0);
    vec3 color = albedo_color.xyz * dotNL;
	
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

		color += rayPayload.color * 0.1;
	}

	// Basic lighting
	rayPayload.color = color;
	rayPayload.distance = gl_RayTmaxEXT;
	rayPayload.normal = world_normal;

}
