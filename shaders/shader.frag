#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord0;
layout(location = 2) in vec2 fragTexCoord1;
layout(location = 3) in vec3 fragNorm;
layout(location = 4) in vec3 fragWorldPos;
layout(location = 5) in vec4 fragTangent;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 invView;
    mat4 proj;
} ubo;

layout (binding = 1) uniform UBOParams {
	vec4 lightDir;
	float envRot;
	float exposure;
	mat4 SHRed;
	mat4 SHGreen;
	mat4 SHBlue;
} uboParams;

layout(binding = 2) uniform sampler2D envMap;

// Material bindings

layout(binding = 3) uniform sampler2D colorMap;
layout(binding = 4) uniform sampler2D metallicRoughnessMap;
layout(binding = 5) uniform sampler2D normalMap;
layout(binding = 6) uniform sampler2D aoMap;
layout(binding = 7) uniform sampler2D emissiveMap;

layout (push_constant) uniform Material {
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
} material;

layout(location = 0) out vec4 outColor;


#define specular_level 0.5
#define occlusion_intensity 1.0

#define nbSamples 32

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

// lighting
float LightAttenuation(vec3 L, vec3 D, float dist, float attn, float inner_rim, float outer_rim) {
	float k = 1.0;
	if (attn > 0.0)
		k = max(1.0 - dist * attn, 0.0); // distance attenuation

	if (outer_rim > 0.0) {
		float c = dot(L, D);
		k *= clamp(1.0 - (c - inner_rim) / (outer_rim - inner_rim), 0.0, 1.0); // spot attenuation
	}
	return k;
}

float SampleHardShadow(sampler2DShadow map, vec4 coord, float bias) {
	vec3 uv = coord.xyz / coord.w;
	return 1.0; // shadow2D(map, vec3(uv.xy, uv.z - bias));
}

float SampleShadowPCF(sampler2DShadow map, vec4 coord, float inv_pixel_size, float bias, vec4 jitter) {
	float k_pixel_size = inv_pixel_size * coord.w;

	float k = 0.0;

#if FORWARD_PIPELINE_AAA
	#define PCF_SAMPLE_COUNT 2.0 // 3x3

	for (float j = 0.0; j <= PCF_SAMPLE_COUNT; ++j) {
		float v = (j + jitter.y) / PCF_SAMPLE_COUNT * 2.0 - 1.0;
		for (float i = 0.0; i <= PCF_SAMPLE_COUNT; ++i) {
			float u = (i + jitter.x) / PCF_SAMPLE_COUNT * 2.0 - 1.0;
			k += SampleHardShadow(map, coord + vec4(vec2(u, v) * k_pixel_size, 0.0, 0.0), bias);
		}
	}

	k /= (PCF_SAMPLE_COUNT + 1.0) * (PCF_SAMPLE_COUNT + 1.0);
#else
	// 2x2
	k += SampleHardShadow(map, coord + vec4(vec2(-0.5, -0.5) * k_pixel_size, 0.0, 0.0), bias);
	k += SampleHardShadow(map, coord + vec4(vec2( 0.5, -0.5) * k_pixel_size, 0.0, 0.0), bias);
	k += SampleHardShadow(map, coord + vec4(vec2(-0.5,  0.5) * k_pixel_size, 0.0, 0.0), bias);
	k += SampleHardShadow(map, coord + vec4(vec2( 0.5,  0.5) * k_pixel_size, 0.0, 0.0), bias);

	k /= 4.0;
#endif

	return k;
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
	pos.x += uboParams.envRot;

	return textureLod(envMap, pos, lod).xyz * uboParams.exposure;
}

vec3 envIrradiance(vec3 dir)
{
	float rot = uboParams.envRot * M_2PI;
	float crot = cos(rot);
	float srot = sin(rot);
	dir = vec3(
		-dir.x * crot - dir.z * srot,
		-dir.y,
		-dir.x * srot + dir.z * crot);
	vec4 shDir = vec4(dir.xzy, 1.0);
	shDir.z = -shDir.z;
	return max(vec3(0.0), vec3(
		dot(shDir, shDir* uboParams.SHRed),
		dot(shDir, shDir* uboParams.SHGreen),
		dot(shDir, shDir* uboParams.SHBlue)
	)) * uboParams.exposure;
}

/*
const float c1 = 0.429043;
const float c2 = 0.511664;
const float c3 = 0.743125;
const float c4 = 0.886227;
const float c5 = 0.247708;

vec3 envIrradiance(vec3 dir)
{
float rot = uboParams.envRot * M_2PI;
float crot = cos(rot);
float srot = sin(rot);
dir = vec3(
dir.x * crot - dir.z * srot,
dir.y,
dir.x * srot + dir.z * crot);

vec3 normal = dir.xzy;

return (c1 * L22 * (normal.x*normal.x - normal.y*normal.y) + c3 * L20 * normal.z*normal.z + c4 * L00 - c5 * L20 + 2.0 * c1 * (L2_2 * normal.x * normal.y + L21 * normal.x * normal.z + L2_1 * normal.y * normal.z) + 2.0 * c2 * (L11 * normal.x + L1_1 * normal.y + L10 * normal.z)) * uboParams.exposure;
}
*/
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

	for (int i = 0; i < nbSamples; ++i)
	{
		//vec2 Xi = fibonacci2D(i, nbSamples);
		vec2 Xi = Hammersley(i, nbSamples);		// no bitwise on webgl:  bit-wise operator supported in GLSL ES 3.00 and above only

		//	vec3 Hn = importanceSampleGGX(Xi, tangent, bitangent, normal, roughness);
		vec3 Hn = ImportanceSampleGGX(Xi, normal, roughness, tangent, bitangent);

		vec3 Ln = normalize(-reflect(R, Hn));
		vec3 LnE = normalize(-reflect(eye, Hn));

		// remove it for now, it seems to be a good idea from marmoset but i prefer artefact than black void
		// in fact enable it
		// https://tinyurl.com/y2hrpo2f
		float fade = horizonFading(dot(vertex_normal, Ln), horizonFade);

		float ndl = max(1e-8, dot(normal, Ln));
		float vdh = max(1e-8, dot(R, Hn));
		float ndh = max(1e-8, dot(normal, Hn));

		float lodS = roughness < 0.01 ? 0.0 : computeLOD(Ln, probabilityGGX(ndh, vdh, roughness));

		radiance += fade * envSampleLOD(LnE, lodS) * cook_torrance_contrib(vdh, ndh, ndl, ndv, specColor, roughness);
	}
	// Remove occlusions on shiny reflections
	float glossiness = 1.0 - roughness;
	radiance *= mix(occlusion, 1.0, glossiness * glossiness) / float(nbSamples);

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

// Entry point of the forward pipeline default uber shader (Phong and PBR)
void main() {

	vec4 albedo_color = texture(colorMap, material.baseColorTextureSet == 0 ? fragTexCoord0 : fragTexCoord1 );
	//albedo_color.xyz = albedo_color.xyz * material.baseColorFactor.xyz;
	albedo_color.xyz = sRGB2linear(albedo_color.xyz) * material.baseColorFactor.xyz;
	albedo_color.w = albedo_color.w * material.baseColorFactor.w;

	if(material.alphaMask == 2 && albedo_color.a < material.alphaMaskCutoff)
		discard;
	 
	float occlusion = texture(aoMap, material.occlusionTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).r;
	float metalness = texture(metallicRoughnessMap, material.metallicRoughnessTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).b * material.metallicFactor;
	float roughness = texture(metallicRoughnessMap, material.metallicRoughnessTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).g * material.roughnessFactor;

	
	vec3 emissive = texture(emissiveMap, material.emissiveTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).xyz * material.emissiveFactor;

	//
	vec3 view = (ubo.view * vec4(fragWorldPos, 1.0)).xyz;
	vec3 P = fragWorldPos; // fragment world pos
	vec3 V = normalize(GetT(ubo.invView) - P); // view vector	

	// normal
	vec3 N = normalize(fragNorm);
	vec3 T = normalize(fragTangent.xyz);
	vec3 B = fragTangent.w * cross(fragNorm, fragTangent.xyz);

	mat3 TBN = mat3(T, B, N);
	
	if( material.normalTextureSet >= 0){
		vec3 tangentNormal = texture(normalMap, material.normalTextureSet == 0 ? fragTexCoord0 : fragTexCoord1).xyz * 2.0 - 1.0;			
		N = normalize( TBN *tangentNormal);
	}

	float specular_level_value = 1.0 - specular_level;
	vec3 specular_color = mix(vec3(0.08 * specular_level_value), albedo_color.xyz, vec3(metalness));
	

	vec3 color_with_light = vec3(0.0, 0.0, 0.0);


//	// SLOT 1: point/spot light (with optional shadows)
//	{
//		vec3 L = P - uLightPos[1].xyz;
//		float distance = length(L);
//		L /= distance;
//		float attenuation = LightAttenuation(L, uLightDir[1].xyz, distance, uLightPos[1].w, uLightDir[1].w, uLightDiffuse[1].w);
//		vec3 radiance = uLightDiffuse[1].xyz * attenuation;
//
////#if SLOT1_SHADOWS
////		radiance *=SampleShadowPCF(uSpotShadowMap, vSpotShadowCoord, uShadowState.y, uShadowState.w, jitter);
////#endif
//		if (length(radiance) > 0.0)
//			color_with_light += pbrComputeSpecularForSphericalLight(V, N, uLightPos[1].xyz - P, uLightInfo[1].x, uLightInfo[1].y, T, B, specular_color, roughness, occlusion) *radiance;
//	}

	vec3 diffuse_color = pbrComputeDiffuse(N, albedo_color.xyz * (1.0 - metalness), occlusion);
	
	vec3 specular_pbr = pbrComputeSpecular(V, N, normalize(fragNorm), T, B, specular_color, roughness, occlusion);
	//%constant% = tonemapReinhard(envIrradiance(normalize(v_normal)));
//	vec3 color = tonemapReinhard(diffuse_color + specular_pbr + sRGB2linear(emissive.xyz));
//	vec3 color = tonemapFilmic(diffuse_color + specular_pbr + sRGB2linear(emissive.xyz));
	vec3 color = diffuse_color + specular_pbr + sRGB2linear(emissive.xyz) + color_with_light;
	
	float opacity = albedo_color.w;

	outColor = vec4(linear2sRGB(color), opacity);
	//outColor = vec4(diffuse_color + specular_pbr, opacity);

	
}
