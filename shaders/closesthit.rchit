#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

struct RayPayload {
	vec3 color;
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
	bool doubleSided;
	uint albedoTex;
	uint metallicRoughnessTex;
	uint aoTex;
	uint normalTex;
	uint emissiveTex;
	uint transmissionTex;

	float metallicFactor;
	float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;
	float transmissionFactor;
	float ior;

	int colorTextureSet;
	int metallicRoughnessTextureSet;
	int normalTextureSet;
	int occlusionTextureSet;
	int emissiveTextureSet;
	int transmissionTextureSet;
	vec4 baseColorFactor;
	vec3 emissiveFactor;
};


layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 0) uniform UBO {
	mat4 viewInverse;
	mat4 projInverse;
	vec4 lightPos;
	mat4 SHRed;
	mat4 SHGreen;
	mat4 SHBlue;
} ubo;

layout(binding = 3, set = 0) buffer Vertices {Vertex v[]; } vertices;
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices;
layout(binding = 5, set = 0) buffer OffsetPrims { OffsetPrim v[]; } offsetPrims;
layout(binding = 6, set = 0) uniform sampler2D texturesMap[];
layout(binding = 7, set = 0) buffer MaterialMap {Material v[]; } materialsMap;
layout(binding = 8, set = 0) uniform sampler2D envMap;


// Max. number of recursion is passed via a specialization constant
layout (constant_id = 0) const int MAX_RECURSION = 3;

#define specular_level 0.5
#define occlusion_intensity 1.0

#define nbSamples 8

#define horizonFade 1.3 // 0.0 to 2.0

#define maxLod 9.0

const float M_PI = 3.141592653589793;
#define M_HALF_PI (M_PI * 0.5)
const float M_2PI = 2.0 * M_PI;
const float M_INV_PI = 0.31830988;
#define M_INV_LOG2 1.442695
#define M_GOLDEN_RATIO 1.618034

vec3 GetT(mat4 m) { 
	return vec3(m[3][0], m[3][1], m[3][2]);
}

//
float sRGB2linear(float v) {
	return (v < 0.04045) ? (v * 0.0773993808) : pow((v + 0.055) / 1.055, 2.4);
}

vec3 sRGB2linear(vec3 v) {
	return vec3(sRGB2linear(v.x), sRGB2linear(v.y), sRGB2linear(v.z));
}

// pbr
float linear2sRGB(float x)
{
	return x <= 0.0031308 ?
		12.92 * x :
		1.055 * pow(x, 0.41666) - 0.055;
}

vec3 linear2sRGB(vec3 rgb)
{
	return vec3(
		linear2sRGB(rgb.x),
		linear2sRGB(rgb.y),
		linear2sRGB(rgb.z));
}

vec3 envSampleLOD(vec3 dir, float lod)
{
	// WORKAROUND: Intel GLSL compiler for HD5000 is bugged on OSX:
	// https://bugs.chromium.org/p/chromium/issues/detail?id=308366
	// It is necessary to replace atan(y, -x) by atan(y, -1.0 * x) to force
	// the second parameter to be interpreted as a float
	vec2 pos = M_INV_PI * vec2(atan(-dir.z, 1.0 * dir.x), 2.0 * asin(-dir.y));
	pos = 0.5 * pos + vec2(0.5);
	pos.x += 0.0;//ubo.envRot;

	return textureLod(envMap, pos, lod).xyz;// * ubo.exposure;
}

vec3 envIrradiance(vec3 dir)
{
	float rot = /*ubo.envRot **/ M_2PI;
	float crot = cos(rot);
	float srot = sin(rot);
	dir = vec3(
		-dir.x * crot - dir.z * srot,
		-dir.y,
		-dir.x * srot + dir.z * crot);
	vec4 shDir = vec4(dir.xzy, 1.0);
	shDir.z = -shDir.z;
	return max(vec3(0.0), vec3(
		dot(shDir, shDir* ubo.SHRed),
		dot(shDir, shDir* ubo.SHGreen),
		dot(shDir, shDir* ubo.SHBlue)
	));// * ubo.exposure;
}

const float EPSILON_COEF = 1e-4;

float normal_distrib(
	float ndh,
	float Roughness)
{
	// use GGX / Trowbridge-Reitz, same as Disney and Unreal 4
	// cf http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf p3
	float alpha = Roughness * Roughness;
	float tmp = alpha / max(1e-8, (ndh * ndh * (alpha * alpha - 1.0) + 1.0));
	return tmp * tmp * M_INV_PI;
}

vec3 fresnel(
	float vdh,
	vec3 F0)
{
	// Schlick with Spherical Gaussian approximation
	// cf http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf p3
	float sphg = pow(2.0, (-5.55473 * vdh - 6.98316) * vdh);
	return F0 + (vec3(1.0) - F0) * sphg;
}

float G1(
	float ndw, // w is either Ln or Vn
	float k)
{
	// One generic factor of the geometry function divided by ndw
	// NB : We should have k > 0
	return 1.0 / (ndw * (1.0 - k) + k);
}

float visibility(
	float ndl,
	float ndv,
	float Roughness)
{
	// Schlick with Smith-like choice of k
	// cf http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf p3
	// visibility is a Cook-Torrance geometry function divided by (n.l)*(n.v)
	float k = max(Roughness * Roughness * 0.5, 1e-5);
	return G1(ndl, k) * G1(ndv, k);
}

vec3 cook_torrance_contrib(
	float vdh,
	float ndh,
	float ndl,
	float ndv,
	vec3 Ks,
	float Roughness)
{
	// This is the contribution when using importance sampling with the GGX based
	// sample distribution. This means ct_contrib = ct_brdf / ggx_probability
	return fresnel(vdh, Ks) *(visibility(ndl, ndv, Roughness) * vdh * ndl / ndh);
}

vec3 importanceSampleGGX(vec2 Xi, vec3 T, vec3 B, vec3 N, float roughness)
{
	float a = roughness * roughness;
	float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	float sinT = sqrt(1.0 - cosT * cosT);
	float phi = 2.0 * M_PI * Xi.x;
	return
		T * (sinT * cos(phi)) +
		B * (sinT * sin(phi)) +
		N * cosT;
}

float fibonacci1D(int i)
{
	return fract((float(i) + 1.0) * M_GOLDEN_RATIO);
}
vec2 fibonacci2D(int i, int nbSamples_)
{
	return vec2((float(i) + 0.5) / float(nbSamples_), fibonacci1D(i));
}
// // no bitwise on webgl:  bit-wise operator supported in GLSL ES 3.00 and above only
// ----------------------------------------------------------------------------
// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// efficient VanDerCorpus calculation.
float RadicalInverse_VdC(int bits)
{
bits = (bits << 16) | (bits >> 16);
bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// ----------------------------------------------------------------------------
vec2 Hammersley(int i, int N)
{
return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}
			// ----------------------------------------------------------------------------
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness, vec3 tangent, vec3 bitangent)
{
	float a = roughness * roughness;

	float phi = 2.0 * M_PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

	// from spherical coordinates to cartesian coordinates - halfway vector
	vec3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}

float probabilityGGX(float ndh, float vdh, float Roughness)
{
	return normal_distrib(ndh, Roughness) * ndh / (4.0 * vdh);
}

float distortion(vec3 Wn)
{
	// Computes the inverse of the solid angle of the (differential) pixel in
	// the cube map pointed at by Wn
	float sinT = sqrt(1.0 - Wn.y * Wn.y);
	return sinT;
}

float computeLOD(vec3 Ln, float p)
{
	ivec2 size = textureSize(envMap, 0);
	float mipmapLevel = floor(log2(max(float(size.x), float(size.y))));
	return max(0.0, (mipmapLevel)-0.5 * log2(float(nbSamples) * p * distortion(Ln)));
	//	  return max(0.0, (maxLod-1.5) - 0.5 * log2(float(nbSamples) * p * distortion(Ln)));
}
float horizonFading(float ndl, float horizonFade_)
{
	float horiz = clamp(1.0 + horizonFade_ * ndl, 0.0, 1.0);
	return horiz * horiz;
}

vec3 pbrComputeSpecular(vec3 eye, vec3 normal, vec3 vertex_normal, vec3 tangent, vec3 bitangent, vec3 specColor, float roughness, float occlusion)
{
	vec3 R = eye;
	if (dot(eye, normal) < 0.0)
		R = reflect(eye, normal);

	vec3 radiance = vec3(0.0);
	float ndv = dot(R, normal);

	vec3 origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;		
	float tmin = 0.001;
	float tmax = 10000.0;
	
	//int current_nb_samples = int(max(clamp(roughness * 3.0 * float(nbSamples), 1.0, float(nbSamples) * 3.0) * ((MAX_RECURSION-rayPayload.currentRecursion) / float(MAX_RECURSION)), 1));
	//int current_nb_samples = int(max(nbSamples * pow(((MAX_RECURSION-rayPayload.currentRecursion) / float(MAX_RECURSION)), 3.0), 1));
	int current_nb_samples = rayPayload.currentRecursion == 0 ? nbSamples : 1;

	for (int i = 0; i < current_nb_samples; ++i)
	{
		//vec2 Xi = fibonacci2D(i, nbSamples);
		vec2 Xi = Hammersley(i, current_nb_samples);		// no bitwise on webgl:  bit-wise operator supported in GLSL ES 3.00 and above only

		//	vec3 Hn = importanceSampleGGX(Xi, tangent, bitangent, normal, roughness);
		vec3 Hn = ImportanceSampleGGX(Xi, normal, roughness, tangent, bitangent);

		vec3 Ln = normalize(-reflect(R, Hn));
		vec3 LnE = normalize(-reflect(eye, Hn));

		float ndl = max(1e-8, dot(normal, Ln));
		float vdh = max(1e-8, dot(R, Hn));
		float ndh = max(1e-8, dot(normal, Hn));

		// reflection
		vec3 reflectValue;

		rayPayload.currentRecursion += 1;
		if(rayPayload.currentRecursion < MAX_RECURSION){			
			traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin, tmin, normalize(reflect(eye, Hn)), tmax, 0);
			reflectValue = rayPayload.color;
		}else{
			// remove it for now, it seems to be a good idea from marmoset but i prefer artefact than black void
			// in fact enable it
			// https://tinyurl.com/y2hrpo2f
			float fade = horizonFading(dot(vertex_normal, Ln), horizonFade);

			float lodS = roughness < 0.01 ? 0.0 : computeLOD(Ln, probabilityGGX(ndh, vdh, roughness));				
			reflectValue = fade * envSampleLOD(LnE, lodS);
		}
		rayPayload.currentRecursion -= 1;

		radiance += reflectValue * cook_torrance_contrib(vdh, ndh, ndl, ndv, specColor, roughness);
	}
	// Remove occlusions on shiny reflections
	float glossiness = 1.0 - roughness;
	radiance *= mix(occlusion, 1.0, glossiness * glossiness) / float(current_nb_samples);

	return radiance;
}

// spherical area light, costy but nice
vec3 pbrComputeSpecularForSphericalLight(vec3 eye, vec3 normal, vec3 posToLight, float radius, float intensity, vec3 tangent, vec3 bitangent, vec3 specColor, float roughness, float occlusion)
{
	intensity *= 1000.0;
	vec3 radiance = vec3(0.0);
	// methode de bourrin 
	int nbSamplesWithRoughness = int(clamp(roughness * 3.0 * float(nbSamples), 1.0, float(nbSamples) * 3.0));

	// light compute from pixar paper
	// https://graphics.pixar.com/library/PhysicallyBasedLighting/paper.pdf
	float radius2 = radius * radius;
	vec3 lightCenterDir = posToLight;
	float d2 = dot(lightCenterDir, lightCenterDir);

	// if we are outside the light
	if (d2 - radius2 >= 1e-4)
	{
		float ndv = dot(eye, normal);

		for (int i = 0; i < nbSamplesWithRoughness; ++i)
		{	//vec2 Xi = fibonacci2D(i, nbSamples);
			vec2 Xi = Hammersley(i, nbSamplesWithRoughness);// no bitwise on webgl:  bit-wise operator supported in GLSL ES 3.00 and above only

			//	vec3 Hn = importanceSampleGGX(Xi, tangent, bitangent, normal, roughness);
			vec3 Hn = ImportanceSampleGGX(Xi, normal, roughness, tangent, bitangent);
			vec3 Ln = normalize(-reflect(eye, Hn));
						
			// value from the sphere
			float b = 2.0 * dot(Ln, -lightCenterDir);
			float c = d2 - radius2;
			float delta = b * b - 4.0 * c;
			if (delta > 0.0)
			{
				float t = (-b - sqrt(delta)) / 2.0;
				if(t < 1e-5)
					t =  (-b + sqrt(delta)) / 2.0;
				if(t >= 1e-5 && t <= 1e20)
				{ //we have a hit

					// sphere light convolution
					float diffConv = 0.0;
					float cosTheta = dot(lightCenterDir, Ln)/ sqrt(d2);
					float sinAlpha = radius / sqrt(d2);
					float cosAlpha = sqrt(1.0 - sinAlpha * sinAlpha);
					float alpha = asin(sinAlpha);
					float theta = acos(cosTheta);

					if (theta < (M_HALF_PI - alpha))
						diffConv = cosTheta * sinAlpha * sinAlpha;
					else if(theta < M_HALF_PI)
					{
						float g0 = sinAlpha * sinAlpha * sinAlpha;
						float g1 = M_INV_PI * (alpha - cosAlpha * sinAlpha);
						float gp0 = -cosAlpha * (sinAlpha * sinAlpha) * alpha;
						float gp1 = -(sinAlpha * sinAlpha) * alpha / 2.0;
						float a = gp1 + gp0 - 2.0 * (g1 - g0);
						float b = 3.0 * (g1 - g0) - gp1 - 2.0 * gp0;
						float y = (theta - (M_HALF_PI - alpha)) / alpha;
						diffConv = g0 + y * (gp0 + y * (b + y * a));
					}
					else if(theta < M_HALF_PI + alpha)
					{
						float g0 = M_INV_PI * (alpha - cosAlpha * sinAlpha);
						float gp0 = -(sinAlpha * sinAlpha * alpha) / 2.0;
						float a = gp0 + 2.0 * g0;
						float b = -3.0 * g0 - 2.0 * gp0;
						float y = (theta - M_HALF_PI) / alpha;
						diffConv = g0 + y * (gp0 + y * (b + y * a));
					}

					float ndl = max(1e-8, dot(normal, Ln));
					float vdh = max(1e-8, dot(eye, Hn));
					float ndh = max(1e-8, dot(normal, Hn));

				//	float costThetaMax2 = sqrt(1.0 - (radius2 / d2));
				//	float pdf = 1.0 / (M_2PI * (1.0 - costThetaMax2));
					radiance += diffConv * intensity * cook_torrance_contrib(vdh, ndh, ndl, ndv, specColor, roughness);
				}
			}
		}

	}
	// Remove occlusions on shiny reflections
	float glossiness = 1.0 - roughness;
	radiance *= mix(occlusion, 1.0, glossiness * glossiness) / float(nbSamplesWithRoughness);

	return radiance;
}

vec3 tonemapReinhard(vec3 color) {
	return pow(color / (color + vec3(1.0)), vec3(1.0 / 2.2));
}

// http://filmicworlds.com/blog/filmic-tonemapping-operators/
vec3 tonemapFilmic(vec3 color) {
	//		color *= 16; // Hardcoded Exposure Adjustment
	vec3 x = max(vec3(0.0), color - vec3(0.004));
	// pow 2.2 included
	return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

vec3 pbrComputeDiffuse(vec3 normal, vec3 diffColor, float occlusion)
{
	//			  return occlusion * diffColor;
	vec3 irradiance = envIrradiance(normal);
	irradiance = irradiance / (irradiance + vec3(1.0));
	return occlusion * irradiance * diffColor;
	//  return occlusion * clamp(envIrradiance(normal), vec3(0.0),vec3(1.5)) * diffColor;

}

// Hash Functions for GPU Rendering, Jarzynski et al.
// http://www.jcgt.org/published/0009/03/02/
vec3 random_pcg3d(uvec3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y*v.z; v.y += v.z*v.x; v.z += v.x*v.y;
  v ^= v >> 16u;
  v.x += v.y*v.z; v.y += v.z*v.x; v.z += v.x*v.y;
  return vec3(v) * (1.0/float(0xffffffffu));
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
    
    // color values
	vec4 albedo_color = texture(texturesMap[mat.albedoTex], mat.colorTextureSet == 0 ? fragTexCoord0 : fragTexCoord1 );
	//albedo_color.xyz = albedo_color.xyz * mat.baseColorFactor.xyz;
	albedo_color.xyz = sRGB2linear(albedo_color.xyz) * mat.baseColorFactor.xyz;
	albedo_color.w = albedo_color.w * mat.baseColorFactor.w;
	// alpha mask
	if(mat.alphaMask == 2 && albedo_color.a < mat.alphaMaskCutoff){	
		rayPayload.currentRecursion += 1;
		if(rayPayload.currentRecursion < 10)		
			traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT, 0.001, gl_WorldRayDirectionEXT, 10000.0, 0);
		else
			rayPayload.color = vec3(0,1,0);
		rayPayload.currentRecursion -= 1;
		return;
	}
		 
	float occlusion = texture(texturesMap[mat.aoTex], mat.occlusionTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).r;
	float metalness = texture(texturesMap[mat.metallicRoughnessTex], mat.metallicRoughnessTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).b * mat.metallicFactor;
	float roughness = texture(texturesMap[mat.metallicRoughnessTex], mat.metallicRoughnessTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).g * mat.roughnessFactor;
		
	vec3 emissive = texture(texturesMap[mat.emissiveTex], mat.emissiveTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).xyz * mat.emissiveFactor;
	float transmission = texture(texturesMap[mat.transmissionTex], mat.transmissionTextureSet== 0 ? fragTexCoord0 : fragTexCoord1).x * mat.transmissionFactor;

	// normal
	vec3 N = normalize(world_normal);
	vec3 T = normalize(world_tangent.xyz);
	vec3 B = world_tangent.w * cross(world_normal, world_tangent.xyz);

	mat3 TBN = mat3(T, B, N);
	
	if( mat.normalTextureSet >= 0){
		vec3 tangentNormal = texture(texturesMap[mat.normalTex], mat.normalTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).xyz * 2.0 - 1.0;			
		N = normalize( TBN *tangentNormal);
	}

	float specular_level_value = 1.0 - specular_level;
	vec3 specular_color = mix(vec3(0.08 * specular_level_value), albedo_color.xyz, vec3(metalness));
	
	// compute color	
	vec3 diffuse_color = pbrComputeDiffuse(N, albedo_color.xyz * (1.0 - metalness), occlusion);	
	vec3 specular_pbr = pbrComputeSpecular(gl_WorldRayDirectionEXT, N, normalize(world_normal), T, B, specular_color, roughness, occlusion);
//
//	// area light shadow
//	float areaLightSize = 1.5;
//	// launch multiple shadow ray (YES IT S SLOW BUT JUST FOR FUN)
//	float shadowrayCount = 10.0;
//	for(uint i=0; i< uint(shadowrayCount); ++i){
//	  // different random value for each pixel and each frame
//	  vec3 random = random_pcg3d(uvec3(gl_LaunchIDEXT.xy, i/*frameID*/));
//	  vec2 randomLightOffset = random.xy - 0.5;
//	  vec3 randomLightPos = ubo.lightPos.xyz + areaLightSize * vec3(randomLightOffset, 0.0);
//  
//	  vec3 lightDir = normalize(randomLightPos - world_position);
//  
//	  // prepare shadow ray
//	  uint rayFlags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT;
//	  float rayMin     = 0.001;
//	  float rayMax     = length(randomLightPos - world_position);  
//	  float shadowBias = 0.001;
//	  uint cullMask = 0xFFu;
//	  float frontFacing = dot(-gl_WorldRayDirectionEXT, world_normal);
//	  vec3 shadowRayOrigin = world_position + sign(frontFacing) * shadowBias * world_normal;
//	  vec3 shadowRayDirection = lightDir;
//	  shadowed = true;
//  
//	  // shot shadow ray
//	  traceRayEXT(topLevelAS, rayFlags, cullMask, 1, 0, 1, 
//			 shadowRayOrigin, rayMin, shadowRayDirection, rayMax, 2);
//  
//	  // diffuse shading
//		float irradiance = max(dot(lightDir, normal), 0.2);
//		if(shadowed)
//			irradiance = 0.2;
//		radiance += (albedo_color.xyz * irradiance)/shadowrayCount; // diffuse shading
//	 }
//	 
	// dome light shadow
	// launch multiple shadow ray (YES IT S SLOW BUT JUST FOR FUN)
	float shadowrayCount = rayPayload.currentRecursion == 0 ? 10 : 1;
	//float shadowrayCount = 10.0 * pow(((MAX_RECURSION-rayPayload.currentRecursion) / float(MAX_RECURSION)), 3.0);
	float light_intensity_with_shadow = 0.0;
	for(uint i=0; i< uint(shadowrayCount); ++i){
		// different random value for each pixel and each frame
		vec3 random = random_pcg3d(uvec3(gl_LaunchIDEXT.xy, i/*frameID*/));
		vec2 randomLightOffset = random.xy - 0.5;
		vec3 offset = T * randomLightOffset.x + B * randomLightOffset.y;
		vec3 lightDir = normalize(N + offset);
  
		// prepare shadow ray
		uint rayFlags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT;
		float rayMin     = 0.001;
		float rayMax     = 10000.0;  
		float shadowBias = 0.001;
		uint cullMask = 0xFFu;
		float frontFacing = dot(-gl_WorldRayDirectionEXT, world_normal);
		vec3 shadowRayOrigin = world_position + sign(frontFacing) * shadowBias * world_normal;
		vec3 shadowRayDirection = lightDir;
		shadowed = true;
  
		// shot shadow ray
		traceRayEXT(topLevelAS, rayFlags, cullMask, 1, 0, 1, shadowRayOrigin, rayMin, shadowRayDirection, rayMax, 2);

		if(!shadowed)
			light_intensity_with_shadow += 1;
	}
	 
	diffuse_color *= max(light_intensity_with_shadow / shadowrayCount, 0.2);


	// remove fireflies
	specular_pbr = clamp(specular_pbr, vec3(0.0), vec3(1.0)); 

	// refraction
	if(transmission > 0.0)
	{
		rayPayload.currentRecursion += 1;
		if(rayPayload.currentRecursion < MAX_RECURSION){
			vec3 forwardNormal = N;
			float frontFacing = dot(gl_WorldRayDirectionEXT, N);
			float eta = 1.0 / mat.ior;
			if(frontFacing > 0.0) {
				forwardNormal = -N;
				eta = mat.ior;
			} 
			vec3 refractDir = refract(gl_WorldRayDirectionEXT, forwardNormal, eta);
//			traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, world_position, 0.001, refractDir, 10000.0, 0);
//			diffuse_color = mix( diffuse_color, rayPayload.color * albedo_color.xyz,transmission);
			
			// try refraction with roughness
			//int current_nb_samples = int(max(clamp(roughness * 3.0 * float(nbSamples), 1.0, float(nbSamples) * 3.0) * ((MAX_RECURSION-rayPayload.currentRecursion) / float(MAX_RECURSION)), 1));
			//int current_nb_samples = int(max(nbSamples * pow(((MAX_RECURSION-rayPayload.currentRecursion) / float(MAX_RECURSION)), 3.0), 1));
			int current_nb_samples = rayPayload.currentRecursion == 0 ? nbSamples : 1;
			vec3 refractionColor = vec3(0);
			for (int i = 0; i < current_nb_samples; ++i) {
				vec2 Xi = Hammersley(i, current_nb_samples);

				vec3 Hn = ImportanceSampleGGX(Xi, refractDir, roughness, T, B);

				// refraction	
				traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, world_position, 0.001, Hn, 10000.0, 0);
				refractionColor += rayPayload.color;
			}
			diffuse_color = mix(diffuse_color, (refractionColor / float(current_nb_samples)) * albedo_color.xyz,transmission);
		}
		rayPayload.currentRecursion -= 1;
	}

	vec3 color = diffuse_color + specular_pbr + sRGB2linear(emissive.xyz);

	// Basic lighting
	rayPayload.color = color;
}
