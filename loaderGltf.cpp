#include "loaderGltf.h"

#include "core_utils.h"
#include "texture.h"
#include "vertex_config.h"

#define TINYGLTF_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_USE_CPP14
#include <nlohmann/json.hpp>

#include "camera.h"
#include "computeMikkTSpace.h"
#include "loader.h"
#include "tiny_gltf.h"
using namespace tinygltf;

#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

#include <glm/gtc/type_ptr.hpp>

std::map<std::string, textureGLTF> textureCache;

bool LoadImageDataEx(Image *image, const int image_idx, std::string *err, std::string *warn, int req_width, int req_height, const unsigned char *bytes, int size, void *user_data) {
	std::string imageName = image->uri;

	if (image->uri.empty())
		imageName = image->name.empty() ? fmt::format("{}", image_idx) : image->name;

	textureCache[imageName] = textureGLTF{};
	auto &tex = textureCache[imageName];

	tex.name = imageName;
	createTextureImage(bytes, size, tex.textureImage, tex.textureImageMemory, tex.mipLevels);
	tex.textureImageView = createTextureImageView(tex.textureImage, tex.mipLevels, VK_FORMAT_R8G8B8A8_UNORM);
	createTextureSampler(tex.textureSampler, tex.mipLevels);

	return true;
}

std::map<std::string, std::string> primitiveIdsToGeoPath;
std::map<std::string, int> geoPathOcurrence;

static std::string Indent(const int indent) {
	std::string s;
	for (int i = 0; i < indent; i++) {
		s += "  ";
	}
	return s;
}

/// Adapts an array of bytes to an array of T. Will advace of byte_stride each
/// elements.
template <typename T>
struct arrayAdapter {
	/// Pointer to the bytes
	const unsigned char *dataPtr;
	/// Number of elements in the array
	const size_t elemCount;
	/// Stride in bytes between two elements
	const size_t stride;

	/// Construct an array adapter.
	/// \param ptr Pointer to the start of the data, with offset applied
	/// \param count Number of elements in the array
	/// \param byte_stride Stride betweens elements in the array
	arrayAdapter(const unsigned char *ptr, size_t count, size_t byte_stride) : dataPtr(ptr), elemCount(count), stride(byte_stride) {
	}

	/// Returns a *copy* of a single element. Can't be used to modify it.
	T operator[](size_t pos) const {
		if (pos >= elemCount)
			throw std::out_of_range("Tried to access beyond the last element of an array adapter with "
			                        "count " +
			                        std::to_string(elemCount) + " while getting elemnet number " + std::to_string(pos));
		return *(reinterpret_cast<const T*>(dataPtr + pos * stride));
	}
};

/// Interface of any adapted array that returns byte data
struct byteArrayBase {
	virtual ~byteArrayBase() = default;
	virtual unsigned char operator[](size_t) const = 0;
	virtual size_t size() const = 0;
};

/// Interface of any adapted array that returns integer data
struct intArrayBase {
	virtual ~intArrayBase() = default;
	virtual unsigned int operator[](size_t) const = 0;
	virtual size_t size() const = 0;
};

/// Interface of any adapted array that returns float data
struct floatArrayBase {
	virtual ~floatArrayBase() = default;
	virtual float operator[](size_t) const = 0;
	virtual size_t size() const = 0;
};

/// An array that loads interger types, returns them as byte
template <class T>
struct byteArray : public byteArrayBase {
	arrayAdapter<T> adapter;

	explicit byteArray(const arrayAdapter<T> &a) : adapter(a) {
	}

	unsigned char operator[](size_t position) const override { return static_cast<unsigned char>(adapter[position]); }

	size_t size() const override { return adapter.elemCount; }
};

/// An array that loads interger types, returns them as int
template <class T>
struct intArray : public intArrayBase {
	arrayAdapter<T> adapter;

	explicit intArray(const arrayAdapter<T> &a) : adapter(a) {
	}

	unsigned int operator[](size_t position) const override { return static_cast<unsigned int>(adapter[position]); }

	size_t size() const override { return adapter.elemCount; }
};

template <class T>
struct floatArray : public floatArrayBase {
	arrayAdapter<T> adapter;

	explicit floatArray(const arrayAdapter<T> &a) : adapter(a) {
	}

	float operator[](size_t position) const override { return static_cast<float>(adapter[position]); }

	size_t size() const override { return adapter.elemCount; }
};

struct v2fArray {
	arrayAdapter<glm::vec2> adapter;

	explicit v2fArray(const arrayAdapter<glm::vec2> &a) : adapter(a) {
	}

	glm::vec2 operator[](size_t position) const { return adapter[position]; }
	size_t size() const { return adapter.elemCount; }
};

struct v3fArray {
	arrayAdapter<glm::vec3> adapter;

	explicit v3fArray(const arrayAdapter<glm::vec3> &a) : adapter(a) {
	}

	glm::vec3 operator[](size_t position) const { return adapter[position]; }
	size_t size() const { return adapter.elemCount; }
};

struct v4fArray {
	arrayAdapter<glm::vec4> adapter;

	explicit v4fArray(const arrayAdapter<glm::vec4> &a) : adapter(a) {
	}

	glm::vec4 operator[](size_t position) const { return adapter[position]; }
	size_t size() const { return adapter.elemCount; }
};

struct m44fArray {
	arrayAdapter<glm::mat4> adapter;

	explicit m44fArray(const arrayAdapter<glm::mat4> &a) : adapter(a) {
	}

	glm::mat4 operator[](size_t position) const { return adapter[position]; }
	size_t size() const { return adapter.elemCount; }
};

//
//static void ImportMotions(const Model &model, const Scene &gltf_scene, std::vector<objectGLTF> &scene) {
//
//	// all animations
//	spdlog::debug(fmt::format("animations(items={})", model.animations.size()).c_str());
//	for (size_t i = 0; i < model.animations.size(); i++) {
//			const tinygltf::Animation &animation = model.animations[i];
//			spdlog::debug(fmt::format(Indent(1) + "name         : {}", animation.name.empty() ? "anim_" + std::to_string(i) : animation.name).c_str());
//
//			std::vector<objectGLTF>Anim scene_anim{animation.name.empty() ? "anim_" + std::to_string(i) : animation.name, hg::time_from_sec(99999), hg::time_from_sec(-99999)};
//
//			std::vector<hg::AnimRef> anims;
//
//			// add animations samplers
//			spdlog::debug(fmt::format(Indent(1) + "samplers(items={})", animation.samplers.size()).c_str());
//			for (size_t j = 0; j < animation.samplers.size(); j++) {
//			const tinygltf::AnimationSampler &sampler = animation.samplers[j];
//			spdlog::debug(fmt::format(Indent(2) + "input         : {}", sampler.input).c_str());
//			spdlog::debug(fmt::format(Indent(2) + "interpolation : {}", sampler.interpolation).c_str());
//			spdlog::debug(fmt::format(Indent(2) + "output        : {}", sampler.output).c_str());
//
//			// input (usually the time) Get the good buffer from all the gltf micmac
//			const auto inputAccessor = model.accessors[sampler.input];
//			const auto &inputBufferView = model.bufferViews[inputAccessor.bufferView];
//			const auto &inputBuffer = model.buffers[inputBufferView.buffer];
//			const auto inputDataPtr = inputBuffer.data.data() + inputBufferView.byteOffset + inputAccessor.byteOffset;
//			const auto input_byte_stride = inputAccessor.ByteStride(inputBufferView);
//			const auto input_count = inputAccessor.count;
//
//			//	spdlog::debug(fmt::format("input attribute has count {} and stride {} bytes", input_count, input_byte_stride).c_str());
//
//			// output (value for the key) Get the good buffer from all the gltf micmac
//			const auto outputAccessor = model.accessors[sampler.output];
//			const auto &outputBufferView = model.bufferViews[outputAccessor.bufferView];
//			const auto &outputBuffer = model.buffers[outputBufferView.buffer];
//			const auto outputDataPtr = outputBuffer.data.data() + outputBufferView.byteOffset + outputAccessor.byteOffset;
//			const auto output_byte_stride = outputAccessor.ByteStride(outputBufferView);
//			const auto output_count = outputAccessor.count;
//
//			//	spdlog::debug(fmt::format("output attribute has count {} and stride {} bytes", output_count, output_byte_stride).c_str());
//
//			const auto anim_ref = scene.AddAnim({});
//			anims.push_back(anim_ref);
//			auto anim = scene.GetAnim(anim_ref);
//
//			// get channel type anim
//			std::string target_path = "";
//			for (size_t k = 0; k < animation.channels.size(); k++)
//				if (animation.channels[k].sampler == j)
//					target_path = animation.channels[j].target_path;
//
//			// Transfers the value and time from the buffer to harfang depending of the type
//			switch (inputAccessor.type) {
//				case TINYGLTF_TYPE_SCALAR: {
//					switch (inputAccessor.componentType) {
//						case TINYGLTF_COMPONENT_TYPE_FLOAT: {
//							spdlog::debug("Input type is FLOAT");
//							// vector of float
//							floatArray<float> time(arrayAdapter<float>(inputDataPtr, input_count, input_byte_stride));
//
//							spdlog::debug(fmt::format("time's size : {}", time.size()).c_str());
//
//							switch (outputAccessor.type) {
//								case TINYGLTF_TYPE_VEC4: {
//									switch (outputAccessor.componentType) {
//										case TINYGLTF_COMPONENT_TYPE_FLOAT: {
//											spdlog::debug("Output type is FLOAT");
//
//											v4fArray value(arrayAdapter<glm::vec4>(outputDataPtr, output_count, output_byte_stride));
//
//											spdlog::debug(fmt::format("value's size : {}", value.size()).c_str());
//											anim->quat_tracks.push_back({"NO_MATCH", {}});
//											anim->flags |= hg::AF_UseQuaternionForRotation;
//
//											for (size_t k{0}; k < time.size(); ++k) {
//												auto t = hg::time_from_sec_f(time[k]);
//												auto v = value[k];
//												// spdlog::debug(fmt::format("t[{}]: ({}), v[{}]: ({}, %5, %6,
//												// %7)", k, t, k, v.x, v.y, v.z, v.w).c_str());
//												auto r = hg::ToEuler(hg::Quaternion(v.x, v.y, v.z, v.w));
//												hg::SetKey(anim->quat_tracks.back(), t, hg::QuaternionFromEuler(-r.x, -r.y, r.z));
//											}
//											anim->t_start = hg::time_from_sec_f(time[0]);
//											anim->t_end = hg::time_from_sec_f(time[time.size() - 1]);
//
//											scene_anim.t_start = hg::Min(scene_anim.t_start, anim->t_start);
//											scene_anim.t_end = hg::Max(scene_anim.t_end, anim->t_end);
//										} break;
//										default:
//											throw std::runtime_error("Error: Animation values needs to be Float (else not implemented)");
//											break;
//									}
//
//								} break;
//
//								case TINYGLTF_TYPE_VEC3: {
//									switch (outputAccessor.componentType) {
//										case TINYGLTF_COMPONENT_TYPE_FLOAT: {
//											spdlog::debug("Output type is FLOAT");
//
//											v3fArray value(arrayAdapter<glm::vec3>(outputDataPtr, output_count, output_byte_stride));
//
//											spdlog::debug(fmt::format("value's size : {}", value.size()).c_str());
//											anim->vec3_tracks.push_back({"NO_MATCH", {}});
//
//											for (size_t k{0}; k < time.size(); ++k) {
//												auto t = hg::time_from_sec_f(time[k]);
//												auto v = value[k];
//
//												// fix only for pos
//												if (target_path == "translation")
//													v.z = -v.z;
//
//												// spdlog::debug(fmt::format("t[{}]: ({}), v[{}]: ({}, %5,
//												// %6)", k, t, k, v.x, v.y, v.z).c_str());
//												hg::SetKey(anim->vec3_tracks.back(), t, v);
//											}
//											anim->t_start = hg::time_from_sec_f(time[0]);
//											anim->t_end = hg::time_from_sec_f(time[time.size() - 1]);
//
//											scene_anim.t_start = hg::Min(scene_anim.t_start, anim->t_start);
//											scene_anim.t_end = hg::Max(scene_anim.t_end, anim->t_end);
//										} break;
//										default:
//											throw std::runtime_error("Error: Animation values needs to be Float (else not implemented)");
//											break;
//									}
//
//								} break;
//							}
//						} break;
//						default:
//							throw std::runtime_error("Error: Time values needs to be Float (else not implemented)");
//							break;
//					}
//					break;
//				}
//			}
//			}
//
//			// add animation channel, link from the anim track made above to the type of animaton and to the node
//			spdlog::debug((Indent(1) + "channels : [ ").c_str());
//			for (size_t j = 0; j < animation.channels.size(); j++) {
//			spdlog::debug(fmt::format(Indent(2) + "sampler     : {}", animation.channels[j].sampler).c_str());
//			spdlog::debug(fmt::format(Indent(2) + "target.id   : {}", animation.channels[j].target_node).c_str());
//			spdlog::debug(fmt::format(Indent(2) + "target.path : {}", animation.channels[j].target_path).c_str());
//			spdlog::debug((j != (animation.channels.size() - 1)) ? "  , " : "");
//
//			if (animation.channels[j].sampler < anims.size()) {
//				auto anim_ref = anims[animation.channels[j].sampler];
//				auto anim = scene.GetAnim(anim_ref);
//
//				scene_anim.node_anims.push_back({idNode_to_NodeRef[animation.channels[j].target_node], anim_ref});
//
//				std::string target;
//				if (animation.channels[j].target_path == "translation")
//					target = "Position";
//				else if (animation.channels[j].target_path == "rotation")
//					target = "Rotation";
//				else if (animation.channels[j].target_path == "scale")
//					target = "Scale";
//				else if (animation.channels[j].target_path == "weights")
//					target = "Weights";
//
//				// find which track is not empty
//				if (anim->bool_tracks.size())
//					anim->bool_tracks.back().target = target;
//				else if (anim->int_tracks.size())
//					anim->int_tracks.back().target = target;
//				else if (anim->float_tracks.size())
//					anim->float_tracks.back().target = target;
//				else if (anim->vec2_tracks.size())
//					anim->vec2_tracks.back().target = target;
//				else if (anim->vec3_tracks.size())
//					anim->vec3_tracks.back().target = target;
//				else if (anim->vec4_tracks.size())
//					anim->vec4_tracks.back().target = target;
//				else if (anim->quat_tracks.size())
//					anim->quat_tracks.back().target = target;
//			}
//			}
//			spdlog::debug("  ]");
//
//			scene.AddSceneAnim(scene_anim);
//	}
//}

//
//static void ImportSkins(const Model &model, const Scene &gltf_scene, std::vector<objectGLTF> &scene) {
//	// all skins
//	spdlog::debug(fmt::format("skin(items={})", model.skins.size()).c_str());
//	for (size_t gltf_id_node = 0; gltf_id_node < model.nodes.size(); ++gltf_id_node) {
//			const auto &gltf_node = model.nodes[gltf_id_node];
//			if (gltf_node.skin >= 0) {
//			const auto &skin = model.skins[gltf_node.skin];
//			auto node = scene.GetNode(idNode_to_NodeRef[gltf_id_node]);
//			if (auto objectGLTF = node.GetObject()) {
//				objectGLTF.SetBoneCount(skin.joints.size());
//				for (size_t j = 0; j < skin.joints.size(); j++)
//					objectGLTF.SetBone(j, idNode_to_NodeRef[skin.joints[j]]);
//			}
//			}
//	}
//}


static const textureGLTF& ImportTexture(const Model &model, const int &textureIndex, const std::string &imageName) {
	if (textureCache.find(imageName) != textureCache.end()) {
		return textureCache[imageName];
	}
	return textureGLTF{};
	//auto texture = model.textures[textureIndex];
	//uint32_t flags = BGFX_SAMPLER_NONE;
	//if (texture.sampler >= 0) {
	//	auto sampler = model.samplers[texture.sampler];

	//	switch (sampler.wrapS) {
	//		case TINYGLTF_TEXTURE_WRAP_REPEAT:
	//			break; // default
	//		case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
	//			flags |= BGFX_SAMPLER_U_CLAMP;
	//			break;
	//	}
	//	switch (sampler.wrapT) {
	//		case TINYGLTF_TEXTURE_WRAP_REPEAT:
	//			break; // default
	//		case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
	//			flags |= BGFX_SAMPLER_V_CLAMP;
	//			break;
	//	}
	//}

	//std::string dst_rel_path = MakeRelativeResourceName(dst_path, config.prj_path, config.prefix);

	//return resources.textures.Add(dst_rel_path.c_str(), {flags, BGFX_INVALID_HANDLE});
}

//
static const textureGLTF& ImportTexture(const Model &model, const int &textureIndex) {
	if (textureIndex < 0)
		return textureGLTF{};

	const auto &texture = model.textures[textureIndex];
	const auto &image = model.images[texture.source];

	std::string imageName = image.uri;

	if (image.uri.empty())
		imageName = image.name.empty() ? fmt::format("{}", textureIndex) : image.name;

	return ImportTexture(model, textureIndex, imageName);
}

static matGLTF ImportMaterial(const Model &model, const Material &gltf_mat) {
	float glossiness{1.f};
	float reflection{1.f};

	spdlog::debug(fmt::format("Importing material '{}'", gltf_mat.name));

	//
	std::string dst_path;
	matGLTF mat{
		textureCache["WhiteTex"], textureCache["WhiteTex"], textureCache["WhiteTex"], textureCache["WhiteTex"], textureCache["WhiteTex"],
		{
			1,
			1,
			0,
			0,
			0,
			0,
			-1,
			0,
			0,
			{},
			{}
		}
	};

	// BaseColor Texture
	if (const auto &baseColorTexture = ImportTexture(model, gltf_mat.pbrMetallicRoughness.baseColorTexture.index); baseColorTexture.textureImage) {
		spdlog::debug(fmt::format("    - uBaseOpacityMap: {}", baseColorTexture.name));
		mat.albedoTex = baseColorTexture;
		mat.pushConstBlockMaterial.colorTextureSet = gltf_mat.pbrMetallicRoughness.baseColorTexture.texCoord;
	}

	// metallic Roughness Texture
	if (const auto &metallicRoughnessTexture = ImportTexture(model, gltf_mat.pbrMetallicRoughness.metallicRoughnessTexture.index); metallicRoughnessTexture.textureImage) {
		mat.metallicRoughnessTex = metallicRoughnessTexture;
		mat.pushConstBlockMaterial.metallicRoughnessTextureSet = gltf_mat.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
	}

	// ao Texture
	if (const auto &occlusionTexture = ImportTexture(model, gltf_mat.occlusionTexture.index); occlusionTexture.textureImage) {
		mat.aoTex = occlusionTexture;
		mat.pushConstBlockMaterial.occlusionTextureSet = gltf_mat.occlusionTexture.texCoord;
	}

	// normal Texture
	if (const auto &normalTexture = ImportTexture(model, gltf_mat.normalTexture.index); normalTexture.textureImage) {
		mat.normalTex = normalTexture;
		mat.pushConstBlockMaterial.normalTextureSet = gltf_mat.normalTexture.texCoord;
	}

	// emissive Texture
	if (const auto &emissiveTexture = ImportTexture(model, gltf_mat.emissiveTexture.index); emissiveTexture.textureImage) {
		mat.emissiveTex = emissiveTexture;
		mat.pushConstBlockMaterial.emissiveTextureSet = gltf_mat.emissiveTexture.texCoord;
	}

	mat.pushConstBlockMaterial.baseColorFactor = {
		static_cast<float>(gltf_mat.pbrMetallicRoughness.baseColorFactor[0]), static_cast<float>(gltf_mat.pbrMetallicRoughness.baseColorFactor[1]),
		static_cast<float>(gltf_mat.pbrMetallicRoughness.baseColorFactor[2]), static_cast<float>(gltf_mat.pbrMetallicRoughness.baseColorFactor[3])
	};
	mat.pushConstBlockMaterial.roughnessFactor = static_cast<float>(gltf_mat.pbrMetallicRoughness.roughnessFactor);
	mat.pushConstBlockMaterial.metallicFactor = static_cast<float>(gltf_mat.pbrMetallicRoughness.metallicFactor);
	mat.pushConstBlockMaterial.emissiveFactor = {
		static_cast<float>(gltf_mat.emissiveFactor[0]), static_cast<float>(gltf_mat.emissiveFactor[1]), static_cast<float>(gltf_mat.emissiveFactor[2])
	};

	//if (gltf_mat.alphaMode == "BLEND" || gltf_mat.alphaMode == "MASK")
	//		SetMaterialBlendMode(mat, hg::BM_Alpha);

	//if (gltf_mat.doubleSided)
	//		SetMaterialFaceCulling(mat, hg::FC_Disabled);
	//

	return std::move(mat);
}

#define __PolIndex (pol_index[p] + v)
#define __PolRemapIndex (pol_index[p] + (geo.pol[p].vtx_count - 1 - v))

static void ImportGeometry(const Model &model, const Primitive &meshPrimitive, const int &primitiveID, objectGLTF &prim) {
	// TODO detect instancing (using SHA1 on model)

	// Boolean used to check if we have converted the vertex buffer format
	bool convertedToTriangleList = false;
	bool hasTangent = false;
	// This permit to get a type agnostic way of reading the index buffer
	std::unique_ptr<intArrayBase> indicesArrayPtr = nullptr;

	if (meshPrimitive.indices == -1) {
		spdlog::debug("ERROR: Can't load geometry without triangles indices");
		return;
	}

	{
		const auto &indicesAccessor = model.accessors[meshPrimitive.indices];
		const auto &bufferView = model.bufferViews[indicesAccessor.bufferView];
		const auto &buffer = model.buffers[bufferView.buffer];
		const auto dataAddress = buffer.data.data() + bufferView.byteOffset + indicesAccessor.byteOffset;
		const auto byteStride = indicesAccessor.ByteStride(bufferView);
		const auto count = indicesAccessor.count;

		// Allocate the index array in the pointer-to-base declared in the
		// parent scope
		switch (indicesAccessor.componentType) {
			case TINYGLTF_COMPONENT_TYPE_BYTE:
				indicesArrayPtr = std::unique_ptr<intArray<char>>(new intArray<char>(arrayAdapter<char>(dataAddress, count, byteStride)));
				break;

			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				indicesArrayPtr = std::unique_ptr<intArray<unsigned char>>(new intArray<unsigned char>(arrayAdapter<unsigned char>(dataAddress, count, byteStride)));
				break;

			case TINYGLTF_COMPONENT_TYPE_SHORT:
				indicesArrayPtr = std::unique_ptr<intArray<short>>(new intArray<short>(arrayAdapter<short>(dataAddress, count, byteStride)));
				break;

			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				indicesArrayPtr = std::unique_ptr<intArray<unsigned short>>(new intArray<unsigned short>(arrayAdapter<unsigned short>(dataAddress, count, byteStride)));
				break;

			case TINYGLTF_COMPONENT_TYPE_INT:
				indicesArrayPtr = std::unique_ptr<intArray<int>>(new intArray<int>(arrayAdapter<int>(dataAddress, count, byteStride)));
				break;

			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				indicesArrayPtr = std::unique_ptr<intArray<unsigned int>>(new intArray<unsigned int>(arrayAdapter<unsigned int>(dataAddress, count, byteStride)));
				break;
			default:
				break;
		}
	}

	if (indicesArrayPtr) {
		const auto &indices = *indicesArrayPtr;

		for (auto i = 0; i < indices.size(); ++i)
			prim.indices.push_back(indices[i]);
	}

	switch (meshPrimitive.mode) {
		// We re-arrange the indices so that it describe a simple list of
		// triangles
		//case TINYGLTF_MODE_TRIANGLE_FAN:
		//	if (!convertedToTriangleList) {
		//		spdlog::debug("TRIANGLE_FAN");
		//		// This only has to be done once per primitive
		//		convertedToTriangleList = true;

		//		// We steal the guts of the vector
		//		auto triangleFan = std::move(geo.binding);
		//		// geo.binding.clear();

		//		// Push back the indices that describe just one triangle one by one
		//		for (size_t i{start_id_binding + 2}; i < triangleFan.size(); ++i) {
		//			geo.binding.push_back(triangleFan[0]);
		//			geo.binding.push_back(triangleFan[i - 1]);
		//			geo.binding.push_back(triangleFan[i]);
		//		}
		//	}
		//case TINYGLTF_MODE_TRIANGLE_STRIP:
		//	if (!convertedToTriangleList) {
		//		spdlog::debug("TRIANGLE_STRIP");
		//		// This only has to be done once per primitive
		//		convertedToTriangleList = true;

		//		auto triangleStrip = std::move(geo.binding);
		//		// geo.binding.clear();

		//		for (size_t i{start_id_binding + 2}; i < triangleStrip.size(); ++i) {
		//			geo.binding.push_back(triangleStrip[i - 2]);
		//			geo.binding.push_back(triangleStrip[i - 1]);
		//			geo.binding.push_back(triangleStrip[i]);
		//		}
		//	}
		case TINYGLTF_MODE_TRIANGLES: {
			// this is the simpliest case to handle
			spdlog::debug("TRIANGLES");

			using AttribWritter = std::function<void(float *w, uint32_t p)>;
			AttribWritter w_position = [](float *w, uint32_t p) {
			};
			AttribWritter w_normal = [](float *w, uint32_t p) {
			};
			AttribWritter w_texcoord0 = [](float *w, uint32_t p) {
			};
			AttribWritter w_texcoord1 = [](float *w, uint32_t p) {
			};
			AttribWritter w_tangent = [](float *w, uint32_t p) {
			};
			AttribWritter w_joints0 = [](float *w, uint32_t p) {
			};
			AttribWritter w_weights0 = [](float *w, uint32_t p) {
			};

			// get the accessor
			for (const auto &attribute : meshPrimitive.attributes) {
				const auto attribAccessor = model.accessors[attribute.second];
				const auto &bufferView = model.bufferViews[attribAccessor.bufferView];
				const auto &buffer = model.buffers[bufferView.buffer];
				const auto dataPtr = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
				const auto byte_stride = attribAccessor.ByteStride(bufferView);
				const bool normalized = attribAccessor.normalized;

				AttribWritter *writter = nullptr;
				unsigned int max_components = 0;
				if (attribute.first == "POSITION") {
					writter = &w_position;
					max_components = 3;
				} else if (attribute.first == "NORMAL") {
					writter = &w_normal;
					max_components = 3;
				} else if (attribute.first == "TEXCOORD_0") {
					writter = &w_texcoord0;
					max_components = 2;
				} else if (attribute.first == "TEXCOORD_1") {
					writter = &w_texcoord1;
					max_components = 2;
				} else if (attribute.first == "TANGENT") {
					writter = &w_tangent;
					max_components = 4;
				} else if (attribute.first == "JOINTS_0") {
					writter = &w_joints0;
					max_components = 4;
				} else if (attribute.first == "WEIGHTS_0") {
					writter = &w_weights0;
					max_components = 4;
				}

				if (!writter)
					continue;

				switch (attribAccessor.type) {
					case TINYGLTF_TYPE_SCALAR:
						max_components = std::min(max_components, 1u);
						break;
					case TINYGLTF_TYPE_VEC2:
						max_components = std::min(max_components, 2u);
						break;
					case TINYGLTF_TYPE_VEC3:
						max_components = std::min(max_components, 3u);
						break;
					case TINYGLTF_TYPE_VEC4:
						max_components = std::min(max_components, 4u);
						break;
				}

				switch (attribAccessor.componentType) {
					case TINYGLTF_COMPONENT_TYPE_FLOAT:
						*writter = [dataPtr, byte_stride, max_components](float *w, uint32_t p) {
							const float *f = (const float*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = f[i];
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_DOUBLE:
						*writter = [dataPtr, byte_stride, max_components](float *w, uint32_t p) {
							const double *f = (const double*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = static_cast<float>(f[i]);
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_BYTE:
						*writter = [dataPtr, byte_stride, max_components, normalized](float *w, uint32_t p) {
							const int8_t *f = (const int8_t*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = normalized ? f[i] / (float)128 : f[i];
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_SHORT:
						*writter = [dataPtr, byte_stride, max_components, normalized](float *w, uint32_t p) {
							const int16_t *f = (const int16_t*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = normalized ? f[i] / (float)32768 : f[i];
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_INT:
						*writter = [dataPtr, byte_stride, max_components, normalized](float *w, uint32_t p) {
							const int32_t *f = (const int32_t*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = normalized ? f[i] / (float)2147483648 : f[i];
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
						*writter = [dataPtr, byte_stride, max_components, normalized](float *w, uint32_t p) {
							const uint8_t *f = (const uint8_t*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = normalized ? f[i] / (float)255 : f[i];
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
						*writter = [dataPtr, byte_stride, max_components, normalized](float *w, uint32_t p) {
							const uint16_t *f = (const uint16_t*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = normalized ? f[i] / (float)65535 : f[i];
							}
						};
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
						*writter = [dataPtr, byte_stride, max_components, normalized](float *w, uint32_t p) {
							const uint32_t *f = (const uint32_t*)(dataPtr + p * byte_stride);
							for (unsigned int i = 0; i < max_components; ++i) {
								w[i] = normalized ? f[i] / (float)4294967295 : f[i];
							}
						};
						break;
					default:
						assert(!"Not supported component type (yet)");
				}
			}

			// set the vertex first (to have the count) TODO maybe not needed
			for (const auto &attribute : meshPrimitive.attributes) {
				const auto attribAccessor = model.accessors[attribute.second];
				const auto count = attribAccessor.count;

				// spdlog::debug(fmt::format("current attribute has count {} and stride {} bytes", count, byte_stride).c_str());
				spdlog::debug(fmt::format("attribute string is : {}", attribute.first));
				if (attribute.first == "POSITION") {
					spdlog::debug("found position attribute");

					if (prim.vertices.empty())
						prim.vertices.resize(count);


					// get the position min/max for computing the boundingbox
					/*		pMin.x = attribAccessor.minValues[0];
					pMin.y = attribAccessor.minValues[1];
					pMin.z = attribAccessor.minValues[2];
					pMax.x = attribAccessor.maxValues[0];
					pMax.y = attribAccessor.maxValues[1];
					pMax.z = attribAccessor.maxValues[2];
					*/
					// 3D vector of float
					glm::vec3 v;
					for (uint32_t i{0}; i < count; ++i) {
						w_position(&v.x, i);
						prim.vertices[i].pos = v;
					}
					break;
				}
			}

			// set the values
			for (const auto &attribute : meshPrimitive.attributes) {
				const auto attribAccessor = model.accessors[attribute.second];
				const auto count = attribAccessor.count;

				if (attribute.first == "NORMAL") {
					spdlog::debug("found normal attribute");

					glm::vec3 n;
					for (uint32_t i{0}; i < count; ++i) {
						w_normal(&n.x, i);
						prim.vertices[i].norm = n;
					}
				}

				// Face varying comment on the normals is also true for the UVs
				if (attribute.first == "TEXCOORD_0") {
					spdlog::debug("Found texture coordinates 0");

					glm::vec2 uv;
					for (uint32_t i{0}; i < count; ++i) {
						w_texcoord0(&uv.x, i);
						prim.vertices[i].texCoord0 = uv;
					}
				}
				// Face varying comment on the normals is also true for the UVs
				if (attribute.first == "TEXCOORD_1") {
					spdlog::debug("Found texture coordinates 1");

					glm::vec2 uv;
					for (uint32_t i{0}; i < count; ++i) {
						w_texcoord1(&uv.x, i);
						prim.vertices[i].texCoord1 = uv;
					}
				}

				//// JOINTS_0
				//if (attribute.first == "JOINTS_0") {
				//	spdlog::debug("found JOINTS_0 attribute");

				//	if (geo.skin.size() < count + start_id_vtx)
				//		geo.skin.resize(count + start_id_vtx);

				//	glm::vec4 joints;
				//	for (size_t i{0}; i < count; ++i) {
				//		w_joints0(&joints.x, i);
				//		geo.skin[i + start_id_vtx].index[0] = hg::numeric_cast<uint16_t>((int)(joints.x));
				//		geo.skin[i + start_id_vtx].index[1] = hg::numeric_cast<uint16_t>((int)(joints.y));
				//		geo.skin[i + start_id_vtx].index[2] = hg::numeric_cast<uint16_t>((int)(joints.z));
				//		geo.skin[i + start_id_vtx].index[3] = hg::numeric_cast<uint16_t>((int)(joints.w));
				//	}
				//}

				//// WEIGHTS_0
				//if (attribute.first == "WEIGHTS_0") {
				//	spdlog::debug("found WEIGHTS_0 attribute");

				//	if (geo.skin.size() < count + start_id_vtx)
				//		geo.skin.resize(count + start_id_vtx);

				//	glm::vec4 weights;
				//	for (size_t i{0}; i < count; ++i) {
				//		w_weights0(&weights.x, i);
				//		geo.skin[i + start_id_vtx].weight[0] = hg::pack_float<uint8_t>(weights.x);
				//		geo.skin[i + start_id_vtx].weight[1] = hg::pack_float<uint8_t>(weights.y);
				//		geo.skin[i + start_id_vtx].weight[2] = hg::pack_float<uint8_t>(weights.z);
				//		geo.skin[i + start_id_vtx].weight[3] = hg::pack_float<uint8_t>(weights.w);
				//	}
				//}
			}

			// special for the tangent, because it need the normal to compute the bitangent
			for (const auto &attribute : meshPrimitive.attributes) {
				const auto attribAccessor = model.accessors[attribute.second];
				const auto count = attribAccessor.count;

				// spdlog::debug(fmt::format("current attribute has count {} and stride {} bytes", count, byte_stride).c_str());

				spdlog::debug(fmt::format("attribute string is : {}", attribute.first).c_str());
				if (attribute.first == "TANGENT") {
					spdlog::debug("found tangent attribute");
					hasTangent = true;

					glm::vec4 t;
					for (uint32_t i{0}; i < count; ++i) {
						w_normal(&t.x, i);
						prim.vertices[i].tangent = t;
					}
				}
			}
			break;
		}
		default:
			throw std::runtime_error("primitive mode not implemented");
			break;

		// These aren't triangles:
		case TINYGLTF_MODE_POINTS:
		case TINYGLTF_MODE_LINE:
		case TINYGLTF_MODE_LINE_LOOP:
			throw std::runtime_error("primitive is not triangle based, ignoring");
	}


	// check if there is tangent, if not recompile
	if (!hasTangent) {
		spdlog::debug("    - Recalculate tangent frames");
		CalcTangents calc;
		calc.calc(&prim);
	}
}

//
static void ImportObject(const Model &model, const Node &gltf_node, objectGLTF &node, const int &gltf_id_node) {
	// if there is no mesh or no skin, nothing inside objectGLTF
	if (gltf_node.mesh < 0 && gltf_node.skin < 0)
		return;

	std::string path = node.name;
	std::string primitiveIds;

	// Import geo mesh
	if (gltf_node.mesh >= 0) {
		auto gltf_mesh = model.meshes[gltf_node.mesh];

		int primitiveId = 0;
		for (auto meshPrimitive : gltf_mesh.primitives) {
			objectGLTF prim{};
			
			ImportGeometry(model, meshPrimitive, primitiveId, prim);

			// MATERIALS
			if (meshPrimitive.material >= 0) {
				auto gltf_mat = model.materials[meshPrimitive.material];
				prim.mat = ImportMaterial(model, gltf_mat);
				//		if (prim.skin.size())
				//			mat.flags |= hg::MF_EnableSkinning;

				//	objectGLTF.SetMaterial(primitiveId, std::move(mat));
				//	objectGLTF.SetMaterialName(primitiveId, gltf_mat.name.empty() ? fmt::format("mat_{}", primitiveId) : gltf_mat.name);
			} else {
				// make a dummy material to see the objectGLTF in the engine
				spdlog::debug(fmt::format("    - Has no material, set a dummy one"));

				matGLTF mat{
					textureCache["WhiteTex"], textureCache["WhiteTex"], textureCache["WhiteTex"], textureCache["WhiteTex"], textureCache["WhiteTex"],
					{1, 1, 0, 0, 0, 0, -1, 0, 0, {1, 1, 1, 1}, {0,0,0}}
				};
				prim.mat = mat;
			}

			// create VULKAN need

			createDescriptorSetLayout(prim.descriptorSetLayout);
			createGraphicsPipeline("spv/shader.vert.spv", "spv/shader.frag.spv", prim.pipelineLayout, prim.graphicsPipeline, renderPass, msaaSamples, prim.descriptorSetLayout);

			createVertexBuffer(prim.vertices, prim.vertexBuffer, prim.vertexBufferMemory);
			createIndexBuffer(prim.indices, prim.indexBuffer, prim.indexBufferMemory);

			createUniformBuffers(prim.uniformBuffers, prim.uniformBuffersMemory, prim.uniformBuffersMapped);

			createDescriptorPool(prim.descriptorPool);
			createDescriptorSets(prim.descriptorSets, prim.uniformBuffers, prim.mat, prim.descriptorSetLayout, prim.descriptorPool);

			node.children.push_back(std::move(prim));
			++primitiveId;
		}

		//const auto vtx_to_pol = hg::ComputeVertexToPolygon(geo);
		//auto vtx_normal = hg::ComputeVertexNormal(geo, vtx_to_pol, hg::Deg(45.f));

		//// recalculate normals
		//bool recalculate_normal = config.recalculate_normal;
		//if (geo.normal.empty())
		//	recalculate_normal = true;

		//if (recalculate_normal) {
		//	spdlog::debug("    - Recalculate normals");
		//	geo.normal = vtx_normal;
		//} else
		//	vtx_normal = geo.normal;

		//// recalculate tangent frame
		//bool recalculate_tangent = config.recalculate_tangent;
		//if (geo.tangent.empty())
		//	recalculate_tangent = true;
		//else if (geo.tangent.size() != geo.normal.size()) {
		//	// be sure tangent is same size of normal, some strange things can happen with multiple submesh
		//	spdlog::debug("CAREFUL Normal and Tangent are not the same size, can happen if you have submesh (some with tangent and some without)");
		//	geo.tangent.resize(geo.normal.size());
		//}

		//if (recalculate_tangent) {
		//	spdlog::debug("    - Recalculate tangent frames (MikkT)");
		//	if (!geo.uv[0].empty())
		//		geo.tangent = hg::ComputeVertexTangent(geo, vtx_normal, 0, hg::Deg(45.f));
		//}
	}
	// find bind pose in the skins
	//if (gltf_node.skin >= 0) {
	//	spdlog::debug(fmt::format("Importing geometry skin"));

	//	const auto &skin = model.skins[gltf_node.skin];
	//	geo.bind_pose.resize(skin.joints.size());

	//	const auto attribAccessor = model.accessors[skin.inverseBindMatrices];
	//	const auto &bufferView = model.bufferViews[attribAccessor.bufferView];
	//	const auto &buffer = model.buffers[bufferView.buffer];
	//	const auto dataPtr = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
	//	const auto byte_stride = attribAccessor.ByteStride(bufferView);
	//	const auto count = attribAccessor.count;

	//	switch (attribAccessor.type) {
	//		case TINYGLTF_TYPE_MAT4: {
	//			switch (attribAccessor.componentType) {
	//				case TINYGLTF_COMPONENT_TYPE_DOUBLE:
	//				case TINYGLTF_COMPONENT_TYPE_FLOAT: {
	//					floatArray<float> value(arrayAdapter<float>(dataPtr, count * 16, sizeof(float)));

	//					for (size_t k{0}; k < count; ++k) {
	//						glm::mat4 m_InverseBindMatrices(value[k * 16], value[k * 16 + 1], value[k * 16 + 2], value[k * 16 + 4], value[k * 16 + 5], value[k * 16 + 6],
	//						                                value[k * 16 + 8], value[k * 16 + 9], value[k * 16 + 10], value[k * 16 + 12], value[k * 16 + 13],
	//						                                value[k * 16 + 14]);

	//						m_InverseBindMatrices = hg::InverseFast(m_InverseBindMatrices);

	//						auto p = hg::GetT(m_InverseBindMatrices);
	//						p.z = -p.z;
	//						auto r = hg::GetR(m_InverseBindMatrices);
	//						r.x = -r.x;
	//						r.y = -r.y;
	//						auto s = hg::GetS(m_InverseBindMatrices);

	//						geo.bind_pose[k] = hg::InverseFast(hg::TransformationMat4(p, r, s));
	//					}
	//				}
	//				break;
	//				default:
	//					throw std::runtime_error("Unhandeled component type for inverseBindMatrices");
	//			}
	//		}
	//		break;
	//		default:
	//			throw std::runtime_error("Unhandeled MAT4 type for inverseBindMatrices");
	//	}
	//}

	// check if name already taken
	//auto geoPathOcurrence_itr = geoPathOcurrence.find(path);
	//if (geoPathOcurrence_itr != geoPathOcurrence.end()) {
	//	geoPathOcurrence_itr->second++;
	//	path += fmt::format("{}", geoPathOcurrence_itr->second).str();
	//} else
	//	geoPathOcurrence[path] = 0;

	// save it to geo to keep TODO instance style
	//primitiveIdsToGeoPath[primitiveIds] = path;

	if (gltf_node.mesh >= 0 || gltf_node.skin >= 0)
		spdlog::debug(fmt::format("Import geometry to '{}'", path));
}


static void ImportCamera(const Model &model, const Node &gltf_node, objectGLTF &node) {
	//auto camera = scene.CreateCamera();

	//auto gltf_camera = model.cameras[gltf_node.camera];

	//if (gltf_camera.type == "perspective") {
	//		camera.SetZNear(gltf_camera.perspective.znear);
	//		camera.SetZFar(gltf_camera.perspective.zfar);
	//		camera.SetFov(static_cast<float>(gltf_camera.perspective.yfov));
	//		camera.SetIsOrthographic(false);
	//} else if (gltf_camera.type == "orthographic") {
	//		camera.SetZNear(gltf_camera.orthographic.znear);
	//		camera.SetZFar(gltf_camera.orthographic.zfar);
	//		camera.SetIsOrthographic(true);
	//}
	//node.SetCamera(camera);

	camWorld = node.world;
}

//static void ImportLight(const Model &model, const size_t &id_light, hg::Node &node, std::vector<objectGLTF> &scene) {
//	auto light = scene.CreateLight();
//
//	auto gltf_light = model.lights[id_light];
//
//	light.SetDiffuseColor(hg::Color(gltf_light.color[0], gltf_light.color[1], gltf_light.color[2]));
//	light.SetDiffuseIntensity(gltf_light.intensity);
//
//	light.SetInnerAngle(gltf_light.spot.innerConeAngle);
//	light.SetOuterAngle(gltf_light.spot.outerConeAngle);
//
//	light.SetRadius(gltf_light.range);
//
//	if (gltf_light.type == "point") {
//			light.SetType(hg::LT_Point);
//	} else if (gltf_light.type == "directional") {
//			light.SetType(hg::LT_Linear);
//	} else if (gltf_light.type == "spot") {
//			light.SetType(hg::LT_Spot);
//	}
//
//	node.SetLight(light);
//}

//
static objectGLTF ImportNode(const Model &model, const int &gltf_id_node) {
	const auto &gltf_node = model.nodes[gltf_id_node];
	objectGLTF node{};
	node.name = gltf_node.name.empty() ? fmt::format("node{}", gltf_id_node) : gltf_node.name;
	//idNode_to_NodeRef[gltf_id_node] = node.ref;

	// check if disable
	//auto KHR_nodes_disable = gltf_node.extensions.find("KHR_nodes_disable");
	//if (KHR_nodes_disable != gltf_node.extensions.end()) {
	//	if (KHR_nodes_disable->second.Has("visible") && KHR_nodes_disable->second.Get("visible").Get<bool>() == false)
	//		node.Disable();
	//}

	// set transform
	if (gltf_node.matrix.size()) {
		node.world = glm::make_mat4x4(gltf_node.matrix.data());
	} else {
		if (!gltf_node.translation.empty())
			node.world = glm::translate(node.world, glm::vec3(glm::make_vec3(gltf_node.translation.data())));

		if (!gltf_node.rotation.empty()) {
			const glm::quat q = glm::make_quat(gltf_node.rotation.data());
			node.world *= glm::mat4(q);
		}

		if (!gltf_node.scale.empty())
			node.world = glm::scale(node.world, glm::vec3(glm::make_vec3(gltf_node.scale.data())));
	}

	// is it a camera
	if (gltf_node.camera >= 0)
		ImportCamera(model, gltf_node, node);

	//// is it a light
	//auto KHR_lights_punctual = gltf_node.extensions.find("KHR_lights_punctual");
	//if (KHR_lights_punctual != gltf_node.extensions.end()) {
	//		if (KHR_lights_punctual->second.Has("light")) {
	//		auto id_light = KHR_lights_punctual->second.Get("light").Get<int>();
	//		ImportLight(model, id_light, node);
	//		}
	//}

	// is it a mesh
	if (gltf_node.mesh >= 0 || gltf_node.skin >= 0) {
		// if the node doesn't have a name, give the geo name, if there is one
		if (gltf_node.name.empty() && gltf_node.mesh >= 0) {
			auto gltf_mesh = model.meshes[gltf_node.mesh];
			if (!gltf_mesh.name.empty())
				node.name = fs::path(gltf_mesh.name).replace_extension().string();
		}
		ImportObject(model, gltf_node, node, gltf_id_node);
	}

	// import children
	for (auto id_child : gltf_node.children) {
		auto child = ImportNode(model, id_child);
		node.children.push_back(std::move(child));
	}

	return std::move(node);
}

std::vector<objectGLTF> loadSceneGltf(const std::string &scenePath) {
	//
	Model model;
	TinyGLTF loader;
	std::string err;
	std::string warn;

	// set our own save picture
	loader.SetImageLoader(LoadImageDataEx, nullptr);
	bool ret;
	if (fs::path(scenePath).extension() == ".gltf")
		ret = loader.LoadASCIIFromFile(&model, &err, &warn, scenePath);
	else
		ret = loader.LoadBinaryFromFile(&model, &err, &warn, scenePath); // for binary glTF(.glb)
	if (!ret) {
		spdlog::error(fmt::format("failed to load {}: {}", scenePath, err));
		return {};
	}

	if (!warn.empty()) {
		spdlog::info(fmt::format("warning {}: {}", scenePath, warn));
	}

	spdlog::info("loaded glTF file has:");
	spdlog::info(fmt::format("{} accessors", model.accessors.size()));
	spdlog::info(fmt::format("{} animations", model.animations.size()));
	spdlog::info(fmt::format("{} buffers", model.buffers.size()));
	spdlog::info(fmt::format("{} bufferViews", model.bufferViews.size()));
	spdlog::info(fmt::format("{} materials", model.materials.size()));
	spdlog::info(fmt::format("{} meshes", model.meshes.size()));
	spdlog::info(fmt::format("{} nodes", model.nodes.size()));
	spdlog::info(fmt::format("{} textures", model.textures.size()));
	spdlog::info(fmt::format("{} images", model.images.size()));
	spdlog::info(fmt::format("{} skins", model.skins.size()));
	spdlog::info(fmt::format("{} samplers", model.samplers.size()));
	spdlog::info(fmt::format("{} cameras", model.cameras.size()));
	spdlog::info(fmt::format("{} scenes", model.scenes.size()));
	spdlog::info(fmt::format("{} lights", model.lights.size()));

	// create white texture
	{
		std::string imageName("WhiteTex");
		textureCache[imageName] = textureGLTF{};
		auto &tex = textureCache[imageName];

		tex.name = imageName;
		createTextureImage("textures/WhiteTex.png", tex.textureImage, tex.textureImageMemory, tex.mipLevels);
		tex.textureImageView = createTextureImageView(tex.textureImage, tex.mipLevels, VK_FORMAT_R8G8B8A8_UNORM);
		createTextureSampler(tex.textureSampler, tex.mipLevels);
	}

	// Handle only one big scene
	std::vector<objectGLTF> scene;
	for (auto gltf_scene : model.scenes) {
		for (auto gltf_id_node : gltf_scene.nodes) {
			auto node = ImportNode(model, gltf_id_node);
			scene.push_back(std::move(node));
		}

		//ImportMotions(model, gltf_scene, scene, config);
		//ImportSkins(model, gltf_scene, scene, config);		
	}

	return std::move(scene);
}
