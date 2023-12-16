/*
 *  Copyright Vulkanite - 2022  - Thomas Simonnet
 */
#ifdef ACTIVATE_USD

#define NOMINMAX

#include <fstream>
#include <iostream>
#include <mutex>

#undef CopyFile
#undef GetObject

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/sphere.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/usdUtils/dependencies.h"
#include "pxr/usd/usd/primCompositionQuery.h"
#include "pxr/usd/usdLux/sphereLight.h"
#include "pxr/usd/usdLux/distantLight.h"
#include "pxr/usd/usdLux/domeLight.h"
#include "pxr/usd/usdHydra/tokens.h"
#include "pxr/usd/pcp/site.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/ar/packageUtils.h"
#include "pxr/base/gf/vec3f.h"

#include "core_utils.h"
#include "scene.h"
#include "texture.h"
#include "vertex_config.h"

#include <cmrc/cmrc.hpp>
extern cmrc::embedded_filesystem cmrcFS;

#include "camera.h"
#include "computeMikkTSpace.h"
#include "rasterizer.h"

#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <filesystem>
namespace fs = std::filesystem;

#include <glm/gtc/type_ptr.hpp>

void ReplaceAll( std::string& str, const std::string& from, const std::string& to ) {
	if( from.empty() )
		return;
	size_t start_pos = 0;
	while( ( start_pos = str.find( from, start_pos ) ) != std::string::npos ) {
		str.replace( start_pos, from.length(), to );
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
}
//
static void ImportMaterial( const pxr::UsdShadeShader& shaderUSD, std::set<pxr::TfToken>& uvMapVarname, const pxr::UsdStage& stage, matVulkanite& mat ) {

	spdlog::debug( fmt::format( "	Importing material '{}'", shaderUSD.GetPath().GetString() ) );

	//
	std::string dst_path;

	// get all inputs
	for( const auto& input : shaderUSD.GetInputs() ) {
		if( !input )
			continue;

		auto attrs = input.GetValueProducingAttributes();
		if( attrs.size() ) {
			auto baseNameShaderInput = input.GetAttr().GetBaseName().GetString();
			auto attr = attrs[0];
			auto outputShaderName = attr.GetBaseName().GetString();
			//auto y = attr.GetTypeName().GetCPPTypeName();

			// if there is a real value
			if( attr.HasAuthoredValue() ) {
				if( baseNameShaderInput == "diffuseColor" ) {
					pxr::GfVec3f diffuseUSD;
					attr.Get( &diffuseUSD );
					mat.baseColorFactor.x = diffuseUSD.data()[0];
					mat.baseColorFactor.y = diffuseUSD.data()[1];
					mat.baseColorFactor.z = diffuseUSD.data()[2];
				} else if( baseNameShaderInput == "opacity" ) {
					attr.Get( &mat.baseColorFactor.w );
				} else if( baseNameShaderInput == "occlusion" ) {
					attr.Get( &mat.occlusionFactor );
				} else if( baseNameShaderInput == "roughness" ) {
					attr.Get( &mat.roughnessFactor );
				} else if( baseNameShaderInput == "metallic" ) {
					attr.Get( &mat.metallicFactor );
				} else if( baseNameShaderInput == "emissiveColor" ) {
					pxr::GfVec3f emissiveUSD;
					attr.Get( &emissiveUSD );
					mat.emissiveFactor.x = emissiveUSD.data()[0];
					mat.emissiveFactor.y = emissiveUSD.data()[1];
					mat.emissiveFactor.z = emissiveUSD.data()[2];
				}
			} else {
				pxr::UsdShadeShader shaderTexture( attr.GetPrim() );
				//	pxr::ArResolverContextBinder resolverContextBinder( attr.GetPrim().GetStage()->GetPathResolverContext() );

				pxr::TfToken shaderID;
				shaderTexture.GetShaderId( &shaderID );

				// it's a texture
				if( shaderID.GetString() == "UsdUVTexture" ) { // if (shaderID == pxr::UsdHydraTokens->HwUvTexture_1) { //|| shaderID == pxr::UsdHydraTokens->HwPtexTexture_1) {
					for( const auto& inputTexture : shaderTexture.GetInputs() ) {
						auto baseNameTextureInput = inputTexture.GetBaseName().GetString();
						auto attrTexture = inputTexture.GetAttr();
						if( baseNameTextureInput == "file" ) {

							auto y = attrTexture.GetTypeName().GetCPPTypeName();

							// Retrieve the asset file.
							pxr::SdfAssetPath assetPath;
							attrTexture.Get( &assetPath );

							std::hash<std::string> hasher;
							int hashIdentifierPrim = hasher( assetPath.GetAssetPath() );
							if( scene.textureCache.find( hashIdentifierPrim ) == scene.textureCache.end() )
								continue;
							int idTex = scene.textureCache[hashIdentifierPrim]->id;

							// Add the texture to the material.
							if( baseNameShaderInput == "diffuseColor" )
								mat.albedoTex = idTex;
							if( baseNameShaderInput == "opacity" )
								mat.transmissionTex = idTex;

							// Generate the ORM (Occlusion, Roughness, Metallic) using the available values.
							if( baseNameShaderInput == "occlusion" )
								mat.aoTex = idTex;
							if( baseNameShaderInput == "roughness" )
								mat.metallicTex = idTex;
							if( baseNameShaderInput == "metallic" )
								mat.roughnessTex = idTex;

							// Handle the normal texture.
							if( baseNameShaderInput == "normal" ) {
								mat.normalTex = idTex;
								mat.normalTextureSet = 0; // TODO better uv
							}

							// Handle the emissive texture.
							if( baseNameShaderInput == "emissiveColor" )
								mat.emissiveTex = idTex;

						} else if( baseNameTextureInput == "st" && inputTexture.GetConnectedSources().size() > 0 ) {
							// Retrieve the source that is connected to the output.
							auto sourceUV = inputTexture.GetConnectedSources()[0].source;

							// Retrieve the shader where the output is located.
							pxr::UsdShadeShader shaderUV( sourceUV.GetPrim() );

							// Retrieve the UV input.
							auto inputUVName = shaderUV.GetInput( pxr::TfToken( "varname" ) );

							// If there's another connected source, update the inputUVName.
							if( inputUVName.GetConnectedSources().size() > 0 ) {
								auto UVNameSource = inputUVName.GetConnectedSources()[0].source;
								inputUVName = UVNameSource.GetInput( pxr::TfToken( "stPrimvarName" ) );
							}

							// Retrieve the token reference within the geometry.
							pxr::TfToken UVName;
							inputUVName.GetAttr().Get( &UVName );
							uvMapVarname.insert( UVName );
						}
					}
				}
			}
		} else {
			spdlog::error( fmt::format( "!!! Can't find attr for {}", input.GetFullName().GetString() ) );
		}
	}

	if( mat.transmissionTex != 0 || mat.baseColorFactor.w < 1 )
		mat.alphaMask = 1;
}

#define __PolIndex (pol_index[p] + v)
#define __PolRemapIndex (pol_index[p] + (geo.pol[p].vtx_count - 1 - v))

static void ImportGeometry( const pxr::UsdGeomMesh& geoMesh, const pxr::UsdGeomSubset* geoMeshSubSet, primMeshVulkanite& prim, const std::set<pxr::TfToken>& uvMapVarname ) {
	pxr::VtArray<pxr::GfVec3f> points;
	pxr::VtArray<pxr::GfVec3f> normals;
	std::vector<pxr::VtArray<pxr::GfVec2f>> uvs;
	pxr::VtArray<int> faceVertexCounts;
	pxr::VtArray<int> faceVertexIndices;
	pxr::VtArray<int> faceSubsetIndices;

	// apply global scale from usd to be in meter
	const auto globalScale = pxr::UsdGeomGetStageMetersPerUnit( geoMesh.GetPrim().GetStage() );

	// vertices
	geoMesh.GetPointsAttr().Get( &points );

	// normals
	geoMesh.GetNormalsAttr().Get( &normals );

	// faceVertexCounts
	geoMesh.GetFaceVertexCountsAttr().Get( &faceVertexCounts );

	// faceVertexIndices
	geoMesh.GetFaceVertexIndicesAttr().Get( &faceVertexIndices );

	// uv texcoord from blender (TODO test from other sources)
	for( const auto& UVToken : uvMapVarname ) {
		auto UVPrim = pxr::UsdGeomPrimvar( geoMesh.GetPrim().GetAttribute( pxr::TfToken( "primvars:" + UVToken.GetString() ) ) );
		if( UVPrim.HasValue() ) {
			uvs.resize( uvs.size() + 1 );
			UVPrim.Get( &uvs.back() );
		}
	}
	// If a geometry subset exists, retrieve its indices.
	if( geoMeshSubSet )
		geoMeshSubSet->GetIndicesAttr().Get( &faceSubsetIndices );


	spdlog::debug( fmt::format( "	{}: geoMesh.points = {}", __func__, points.size() ) );
	spdlog::debug( fmt::format( "		# of normals = {}", normals.size() ) );
	spdlog::debug( fmt::format( "		# of faceVertexCounts = {}", faceVertexCounts.size() ) );
	spdlog::debug( fmt::format( "		# of faceVertexIndices = {}", faceVertexIndices.size() ) );
	spdlog::debug( fmt::format( "		# of  nb uv = {}", uvs.size() ) );
	spdlog::debug( fmt::format( "		# of faceSubsetIndices = {}", faceSubsetIndices.size() ) );

	// copy index
	/*prim.indices.resize(faceVertexIndices.size());
	memcpy(prim.indices.data(), faceVertexIndices.data(), sizeof(int) * faceVertexIndices.size());

	prim.vertices.resize(points.size());
	for (int j = 0; j < points.size(); ++j)
		memcpy(&prim.vertices[j].pos.x, points[j].data(), sizeof(float) * 3);*/

		// create all polygon
		// TODO for now pos will be duplicated if needed, to optimise one day........
	int count = 0;
	for( int j = 0; j < faceVertexCounts.size(); ++j ) {
		int verticesInPoly = faceVertexCounts[j];

		auto addVertex = [&]( int index ) {
			Vertex v;
			memcpy( &v.pos.x, points[faceVertexIndices[index]].data(), sizeof( float ) * 3 );

			v.norm = glm::vec3( 0, 1, 0 );
			if( normals.size() == points.size() )
				memcpy( &v.norm.x, normals[faceVertexIndices[index]].data(), sizeof( float ) * 3 );
			else
				memcpy( &v.norm.x, normals[index].data(), sizeof( float ) * 3 );

			if( uvs.size() ) {
				if( uvs[0].size() == points.size() )
					memcpy( &v.texCoord0.x, uvs[0][faceVertexIndices[index]].data(), sizeof( float ) * 2 );
				else
					memcpy( &v.texCoord0.x, uvs[0][index].data(), sizeof( float ) * 2 );
				v.texCoord0.y = 1.f - v.texCoord0.y;
			} else
				memset( &v.texCoord0.x, 0, sizeof( float ) * 2 );

			prim.vertices.push_back( v );
			prim.indices.push_back( prim.indices.size() );
		};
		for( size_t i{ 2 }; i < verticesInPoly; ++i ) {
			addVertex( count + 0 );
			addVertex( count + i - 1 );
			addVertex( count + i );
		}
		count += verticesInPoly;
	}


	//// copy the vertex, normal, uv
	//int count = 0;
	//for (auto &v : prim.vertices) {
	//	memcpy(&v.pos.x, points[count].data(), sizeof(float) * 3);
	//	v.pos *= globalScale;
	//	v.norm.x = 1.f;
	//	/*if (count < normals.size())
	//		memcpy(&v.norm.x, normals[count].data(), sizeof(float) * 3);
	//	if (count < uvs.size())
	//		memcpy(&v.texCoord0.x, uvs[count].data(), sizeof(float) * 2);*/

	//	count++;
	//}
	//
	// check if there is tangent, if not recompile
	if( true/*!hasTangent*/ ) {
		spdlog::debug( "    - Recalculate tangent frames" );
		CalcTangents calc;
		calc.calc( &prim );
	}

	//size_t face_offset = 0;
	//for (size_t fid = 0; fid < faceVertexCounts.size(); fid++) {
	//	int f_count = faceVertexCounts[fid];

	//	assert(f_count >= 3);

	//	for (size_t f = 0; f < f_count; f++) {
	//		// indices
	//		prim.indices.push_back(faceVertexIndices[face_offset + (f_count-1-f)]);

	//		// normal x,y,z
	//		if (normals.size()) {
	//			int idx = face_offset + (f_count - 1 - f);
	//			if (normals.size() == points.size())
	//				idx = faceVertexIndices[face_offset + f_count];

	//			hg::Vec3 n(normals[idx][0], normals[idx][1], normals[idx][2]);
	//				
	//			geo.normal.push_back(n);
	//		}

	//		// u, v
	//		for (int i = 0; i < uvs.size(); ++i) {
	//			const auto &uvUSD = uvs[i];
	//			int idx = face_offset + (f_count - 1 - f);
	//			if (normals.size() == points.size())
	//				idx = faceVertexIndices[face_offset + (f_count - 1 - f)];
	//		
	//			hg::Vec2 uv(uvUSD[idx][0], uvUSD[idx][1]);
	//			uv.y = 1.f - uv.y;
	//			geo.uv[i].push_back(uv);
	//		}
	//	}
	//	face_offset += f_count;
	//}

	// If a subset exists, modify the geometry. TODO: This current method is not very efficient, consider optimization.
	//if (faceSubsetIndices.size() > 0) {
	//	std::vector<hg::Geometry::Polygon> pol;
	//	std::vector<uint32_t> binding;
	//	std::vector<hg::Vec3> normal; // per-polygon-vertex

	//	std::array<hg::Geometry::UVSet, 8> uv; // per-polygon-vertex

	//	size_t face_offset = 0;
	//	for (size_t i = 0; i < geo.pol.size(); ++i) {
	//		// find if this poly is in the mesh
	//		for (const auto &j: faceSubsetIndices)
	//			if (i == j) {
	//				pol.push_back(geo.pol[i]);
	//				for (size_t f = 0; f < geo.pol[i].vtx_count; f++) {
	//					// indices
	//					binding.push_back(geo.binding[face_offset + f]);

	//					// normal x,y,z
	//					if (normals.size())
	//						normal.push_back(geo.normal[face_offset + f]);

	//					// u, v
	//					for (int i = 0; i < uvs.size(); ++i)
	//						uv[i].push_back(geo.uv[i][face_offset + f]);
	//				}

	//				break;
	//			}

	//		face_offset += geo.pol[i].vtx_count;
	//	}
	//	geo.pol = pol;
	//	geo.binding = binding;
	//	geo.normal = normal;
	//	geo.uv = uv;
	//}
}

//
//static void GetObjectWithMaterial(const pxr::UsdPrim &p, std::set<pxr::TfToken> &uvMapVarname, matVulkanite &mat) {
//
//	pxr::UsdGeomMesh geoUSD(p);
//
//	std::string path = p.GetPath().GetString();
//
//	// Add material to the primitive.
//	// MATERIALS:
//	// Assign one material per primitive.
//	bool foundMat = false;
//	pxr::UsdShadeMaterialBindingAPI materialBinding(p);
//	auto binding = materialBinding.GetDirectBinding();
//	if (pxr::UsdShadeMaterial shadeMaterial = binding.GetMaterial()) {
//		pxr::UsdShadeShader shader = shadeMaterial.ComputeSurfaceSource();
//
//		// if there is no shader with defaut render context, find the ONE
//		if (!shader) {
//			// find the output surface with the UsdPreviewSurface (we handle this one for now)
//			auto outputs = shadeMaterial.GetSurfaceOutputs();
//			for (const auto &output : outputs) {
//				if (output.HasConnectedSource()) {
//					// get the source connected to the output
//					auto sourceOutput = output.GetConnectedSources()[0].source;
//					auto sourceShaderName = sourceOutput.GetPrim().GetName().GetString();
//					if (sourceShaderName == "UsdPreviewSurface")
//						shader = pxr::UsdShadeShader(sourceOutput.GetPrim());
//				}
//			}
//		}
//
//		if (shader) {
//			foundMat = true;
//
//			// get the material
//			ImportMaterial(shader, uvMapVarname, *p.GetStage(), mat);
//			/*
//			if (geo.skin.size())
//				mat.flags |= hg::MF_EnableSkinning;
//			*/
//
//			// check double side
//			bool isDoubleSided = false;
//			geoUSD.GetDoubleSidedAttr().Get(&isDoubleSided);
//			// if it's a geo subset check the parent 
//			if (p.GetTypeName() == "GeomSubset")
//				pxr::UsdGeomMesh(p.GetParent()).GetDoubleSidedAttr().Get(&isDoubleSided);
//
//			mat.doubleSided = isDoubleSided;
//
//			object.SetMaterial(0, std::move(mat));
//			object.SetMaterialName(0, shader.GetPath().GetString());
//		}else
//			hg::error("!Unexpected shader from UsdShadeShader()");
//	}
//	
//	// If the material is not found, create a dummy material to make the object visible in the engine.
//	if(!foundMat) {
//		spdlog::debug(fmt::format("	- Has no material, set a dummy one"));
//
//		matVulkanite mat;
//
//		// check in case there is special primvars
//		mat.baseColorFactor = {0.5f, 0.5f, 0.5f, 1.f};
//		if (auto diffuseAttr = p.GetAttribute(pxr::TfToken("primvars:displayColor"))) {
//			//auto y = diffuseAttr.GetTypeName().GetCPPTypeName();
//			pxr::VtArray<pxr::GfVec3f> diffuseUSD;
//			diffuseAttr.Get(&diffuseUSD);
//			if (diffuseUSD.size() > 1) {
//				mat.baseColorFactor.x = diffuseUSD[0].data()[0];
//				mat.baseColorFactor.y = diffuseUSD[0].data()[1];
//				mat.baseColorFactor.z = diffuseUSD[0].data()[2];
//			}
//		}
//
//		object.SetMaterial(0, std::move(mat));
//		object.SetMaterialName(0, "dummy_mat");
//	}
//}
std::map<std::string, std::string> protoToInstance;
//
static void ImportObject( const pxr::UsdPrim& p, objectVulkanite* node ) {

	objectVulkanite subMesh{};

	size_t hashIdentifierPrim = 0;
	// get only the last hash identifier
	for( auto o : p.GetPrimIndex().GetNodeRange() ) {
		std::hash<std::string> hasher;
		hashIdentifierPrim = hasher(/*pxr::TfStringify(o.GetLayerStack()) +*/ o.GetPath().GetString() );
	}

	if( hashIdentifierPrim != 0 ) {
		subMesh.id = hashIdentifierPrim;

		// get the prim mesh from the cache
		if( scene.primsMeshCache.contains( subMesh.id ) )
			subMesh.primMesh = subMesh.id;

		// MATERIALS
		//
		pxr::UsdGeomMesh geoUSD( p );
		bool foundMat = false;
		pxr::UsdShadeMaterialBindingAPI materialBinding( p );
		auto binding = materialBinding.GetDirectBinding();
		if( pxr::UsdShadeMaterial shadeMaterial = binding.GetMaterial() ) {
			pxr::UsdShadeShader shader = shadeMaterial.ComputeSurfaceSource();

			// if there is no shader with defaut render context, find the ONE
			if( !shader ) {
				// find the output surface with the UsdPreviewSurface (we handle this one for now)
				auto outputs = shadeMaterial.GetSurfaceOutputs();
				for( const auto& output : outputs ) {
					if( output.HasConnectedSource() ) {
						// get the source connected to the output
						auto sourceOutput = output.GetConnectedSources()[0].source;
						auto sourceShaderName = sourceOutput.GetPrim().GetName().GetString();
						if( sourceShaderName == "UsdPreviewSurface" )
							shader = pxr::UsdShadeShader( sourceOutput.GetPrim() );
					}
				}
			}

			if( shader ) {
				foundMat = true;

				std::hash<std::string> hasher;
				uint32_t hash = hasher( shader.GetPath().GetString() );
				if( scene.materialsCache.find( hash ) != scene.materialsCache.end() &&
					scene.materialsCache[hash] ) {
					subMesh.matCacheID = hash;
					subMesh.mat = scene.materialsCache[subMesh.matCacheID]->id;
				}
			}
		}

		if( !foundMat ) {
			// make a dummy material to see the objectVulkanite in the engine
			spdlog::debug( fmt::format( "    - Has no material, set a dummy one" ) );

			subMesh.mat = 0;
		}

		// create VULKAN needs
		createDescriptorPool( subMesh.descriptorPool );
		createUniformBuffers( subMesh.uniformBuffers, subMesh.uniformBuffersMemory, subMesh.uniformBuffersMapped, sizeof( UniformBufferObject ) );
		createDescriptorSets( subMesh.descriptorSets, subMesh.uniformBuffers, sizeof( UniformBufferObject ), scene.descriptorSetLayout, subMesh.descriptorPool, scene.uniformParamsBuffers, sizeof( UBOParams ) );

		// motion vector
		createDescriptorPoolMotionVector( subMesh.descriptorPoolMotionVector );
		createUniformBuffers( subMesh.uniformBuffersMotionVector, subMesh.uniformBuffersMemoryMotionVector, subMesh.uniformBuffersMappedMotionVector, sizeof( UniformBufferObjectMotionVector ) );
		createDescriptorSetsMotionVector( subMesh.descriptorSetsMotionVector, subMesh.uniformBuffersMotionVector, sizeof( UniformBufferObjectMotionVector ), scene.descriptorSetLayout, subMesh.descriptorPoolMotionVector );

		node->children.push_back( std::move( subMesh ) );
	}

	//	hg::Object object;
//
//	pxr::UsdGeomMesh geoUSD(p);
//	std::string path = p.GetPath().GetString();

//	// If the geometry is not found, import it.
//	if (primToObject.find(hashIdentifierPrim) != primToObject.end()) {
//		object = primToObject[hashIdentifierPrim];
//	} else {
//		// If the geometry is not found, import it.
//		hg::Geometry geo;
//
//		std::set<pxr::TfToken> uvMapVarname;
//
//		object = GetObjectWithMaterial(p, uvMapVarname);
//
//		ExportGeometry(geoUSD, nullptr, geo, uvMapVarname);
//

//
//		// find bind pose in the skins
//	/*	if (gltf_node.skin >= 0) {
//			spdlog::debug(fmt::format("Exporting geometry skin"));
//
//			const auto &skin = model.skins[gltf_node.skin];
//			geo.bind_pose.resize(skin.joints.size());
//
//			const auto attribAccessor = model.accessors[skin.inverseBindMatrices];
//			const auto &bufferView = model.bufferViews[attribAccessor.bufferView];
//			const auto &buffer = model.buffers[bufferView.buffer];
//			const auto dataPtr = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
//			const auto byte_stride = attribAccessor.ByteStride(bufferView);
//			const auto count = attribAccessor.count;
//
//			switch (attribAccessor.type) {
//				case TINYGLTF_TYPE_MAT4: {
//					switch (attribAccessor.componentType) {
//						case TINYGLTF_COMPONENT_TYPE_DOUBLE:
//						case TINYGLTF_COMPONENT_TYPE_FLOAT: {
//							floatArray<float> value(arrayAdapter<float>(dataPtr, count * 16, sizeof(float)));
//
//							for (size_t k{0}; k < count; ++k) {
//								glm::mat4 m_InverseBindMatrices(value[k * 16], value[k * 16 + 1], value[k * 16 + 2], value[k * 16 + 4], value[k * 16 + 5],
//									value[k * 16 + 6], value[k * 16 + 8], value[k * 16 + 9], value[k * 16 + 10], value[k * 16 + 12], value[k * 16 + 13],
//									value[k * 16 + 14]);
//
//								m_InverseBindMatrices = hg::InverseFast(m_InverseBindMatrices);
//
//								auto p = hg::GetT(m_InverseBindMatrices);
//								p.z = -p.z;
//								auto r = hg::GetR(m_InverseBindMatrices);
//								r.x = -r.x;
//								r.y = -r.y;
//								auto s = hg::GetS(m_InverseBindMatrices);
//
//								geo.bind_pose[k] = hg::InverseFast(hg::TransformationMat4(p, r, s));
//							}
//						} break;
//						default:
//							hg::error("Unhandeled component type for inverseBindMatrices");
//					}
//				} break;
//				default:
//					hg::error("Unhandeled MAT4 type for inverseBindMatrices");
//			}
//		}*/
//
//	return object;

}

static void ImportCamera( const pxr::UsdPrim& p ) {
	//auto camera = scene.CreateCamera();
	//nodeParent->SetCamera(camera);

	//auto cameraUSD = pxr::UsdGeomCamera(p);

	//pxr::GfVec2f clippingRange;
	//cameraUSD.GetClippingRangeAttr().Get(&clippingRange);
	//camera.SetZNear(clippingRange[0]);
	//camera.SetZFar(clippingRange[1]);

	//pxr::TfToken projAttr;
	//cameraUSD.GetProjectionAttr().Get(&projAttr);
	//if (projAttr == pxr::UsdGeomTokens->orthographic) {
	//	camera.SetIsOrthographic(true);
	//}else {
	//	float fov;
	//	cameraUSD.GetVerticalApertureAttr().Get(&fov);
	//	camera.SetFov(hg::Deg(fov));
	//	camera.SetIsOrthographic(false);
	//}	
}

static void ExportLight( const pxr::UsdPrim& p, const pxr::TfToken type, objectVulkanite* nodeParent ) {
	//auto light = scene.CreateLight();
	//nodeParent->SetLight(light);
	//pxr::UsdLuxBoundableLightBase lightUSD;
	//if (type == "SphereLight") {
	//	pxr::UsdLuxSphereLight sphereLight(p);
	//	lightUSD = (pxr::UsdLuxBoundableLightBase)sphereLight;
	//	light.SetType(hg::LT_Point);

	//	float radiusAttr;
	//	sphereLight.GetRadiusAttr().Get(&radiusAttr);
	//	light.SetRadius(radiusAttr);

	//} else if (type == "DistantLight") {
	//	pxr::UsdLuxDistantLight distantLight(p);
	//	lightUSD = (pxr::UsdLuxBoundableLightBase)distantLight;
	//	light.SetType(hg::LT_Spot);

	//	float angleAttr;
	//	distantLight.GetAngleAttr().Get(&angleAttr);
	//	light.SetRadius(angleAttr);

	//} else if (type == "DomeLight") {
	//	pxr::UsdLuxDomeLight domeLight(p);
	//	lightUSD = (pxr::UsdLuxBoundableLightBase)domeLight;
	//}	
	//
	//// add common value
	//if (type == "SphereLight" || type == "DistantLight" || type == "DomeLight") {
	//	pxr::GfVec3f colorAttr;
	//	lightUSD.GetColorAttr().Get(&colorAttr);
	//	light.SetDiffuseColor(hg::Color(colorAttr.data()[0], colorAttr.data()[1], colorAttr.data()[2]));
	//}		
}

static glm::mat4 GetXFormMat( const pxr::UsdPrim& p ) {
	auto xForm = pxr::UsdGeomXformable( p );
	pxr::GfMatrix4d transform;
	bool resetsXformStack;
	xForm.GetLocalTransformation( &transform, &resetsXformStack );

	return glm::make_mat4x4( transform.data() );
}

//
static objectVulkanite ImportNode( const pxr::UsdPrim& p, const glm::mat4& parent_world ) {

	auto type = p.GetTypeName();

	spdlog::info( fmt::format( "type: {}, {}", type.GetString(), p.GetPath().GetString().c_str() ) );

	objectVulkanite node{};
	node.name = p.GetName();

	// Transform
	node.world = GetXFormMat( p );

	// Camera
	if( type == "Camera" ) {
		glm::mat4 m(
			1.f, 0.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, -1.f, 0.f, 0.f,
			0.f, 0.f, 0.f, 1.f );
		glm::mat4 flipY(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, -1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);

		glm::mat4 world = parent_world * node.world;
		world = flipY * m * world;
		world = world * glm::rotate( glm::mat4( 1.0f ), glm::radians( 180.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
		updateCamWorld( world );
	}// light
	/*else if (type == "DomeLight" || type == "DistantLight" || type == "SphereLight") {
		ExportLight(p, type, &node);
	}// Mesh
	*/else if( type == "Mesh" ) {
		ImportObject( p, &node );
	}// GeomSubset
	//else if (type == "GeomSubset") {
	//	hg::Object object;

	//	std::string hashIdentifierPrim;
	//	for (auto o : p.GetPrimIndex().GetNodeRange())
	//		hashIdentifierPrim = pxr::TfStringify(o.GetLayerStack()) + o.GetPath().GetText();

	//	// auto j = c.GetPrimIndex().DumpToString();
	//	// If the geometry is not found, import it.
	//	if (primToObject.find(hashIdentifierPrim) != primToObject.end()) {
	//		object = primToObject[hashIdentifierPrim];
	//	} else {
	//		std::string path = p.GetPath().GetString();
	//		spdlog::debug(fmt::format("	add geometry subset", path));
	//		pxr::UsdGeomSubset subsetC(p);
	//		std::set<pxr::TfToken> uvMapVarname;
	//		object = GetObjectWithMaterial(p, uvMapVarname);

	//		hg::Geometry geoSubset;
	//		pxr::UsdGeomMesh geoUSDParent(p.GetParent());
	//		ExportGeometry(geoUSDParent, &subsetC, geoSubset, uvMapVarname);

	//		if (GetOutputPath(path, config.base_output_path, path, {}, "geo", config.import_policy_geometry)) {
	//			spdlog::debug(fmt::format("Export geometry to '{}'", path));
	//			hg::SaveGeometryToFile(path.c_str(), geoSubset);
	//		}

	//		path = MakeRelativeResourceName(path, config.prj_path, config.prefix);
	//		object.SetModelRef(resources.models.Add(path.c_str(), {}));
	//		primToObject[hashIdentifierPrim] = object;
	//	}
	//	node.SetObject(object);

	//	// If it's a subset, make sure to remove the parent mesh object.
	//	nodeParent->SetObject({});

	//} // Sphere
	//else if (type == "Sphere") {
	//	std::set<pxr::TfToken> uvMapVarname;
	//	auto object = GetObjectWithMaterial(p, uvMapVarname);

	//	// FIXME: Unable to save procedural geometry, replace it with a generic model.
	///* bgfx::VertexLayout vs_pos_normal_decl;
	//	vs_pos_normal_decl.begin();
	//	vs_pos_normal_decl.add(bgfx::Attrib::Enum::Position, 3, bgfx::AttribType::Enum::Float);
	//	vs_pos_normal_decl.add(bgfx::Attrib::Enum::Normal, 3, bgfx::AttribType::Enum::Uint8, true, true);
	//	vs_pos_normal_decl.end();
	//	*/
	//	pxr::UsdGeomSphere sphere(p);
	//	float radiusAttr = 1.f;
	//	sphere.GetRadiusAttr().Get(&radiusAttr);
	//	m = m * hg::ScaleMat4(radiusAttr * pxr::UsdGeomGetStageMetersPerUnit(p.GetStage()));

	//	/* auto sphere_model = hg::CreateSphereModel(vs_pos_normal_decl, radiusAttr, 5, 5);
	//	auto sphere_model_ref = resources.models.Add(p.GetPath().GetString().c_str(), sphere_model);
	//	object.SetModelRef(sphere_model_ref);
	//	*/
	//	object.SetModelRef(resources.models.Add("core_library/primitives/sphere.geo", {}));
	//	node.SetObject(object);

	//}

	// load instances
	if( p.IsInstance() ) {
		auto proto = p.GetPrototype();
		auto protoName = proto.GetName().GetString();
		std::string out_path_proto;
		//if (protoToInstance.find(protoName) != protoToInstance.end()) {
		//	out_path_proto = protoToInstance[protoName];
		//} else {
		//	hg::Scene sceneProto;
		//	auto nodeProto = sceneProto.CreateNode(protoName);
		//	nodeProto.SetTransform(sceneProto.CreateTransform());

		//	for (auto c : p.GetPrototype().GetChildren())
		//		ImportNode(c, &nodeProtoProto);

		//	nodeProto.GetTransform().SetParent(node.ref);

		//	if (GetOutputPath(out_path_proto, config.base_output_path, protoName, {}, "scn", config.import_policy_scene))
		//		SaveSceneJsonToFile(out_path_proto.c_str()Proto, resources);

		//	out_path_proto = MakeRelativeResourceName(out_path_proto, config.prj_path, config.prefix);
		//	protoToInstance[protoName] = out_path_proto;
		//}

		//node.SetInstance(scene.CreateInstance(out_path_proto));
	}

	// import children
	for( auto c : p.GetChildren() ) {
		auto type = c.GetTypeName();
		if( type == "Material" || type == "Shader" ) // don't import node to scene for these types
			continue;
		auto child = ImportNode( c, parent_world * node.world );
		node.children.push_back( std::move( child ) );
	}

	return node;
}

void ImportTexture( pxr::UsdStageRefPtr stage ) {

	for( const auto& p : stage->TraverseAll() ) {
		// look for usdUvTexture in all prim
		if( pxr::UsdAttribute attr = p.GetAttribute( pxr::UsdShadeTokens->infoId ) ) {
			pxr::TfToken infoId;
			attr.Get( &infoId );
			if( infoId.GetString() == "UsdUVTexture" ) {//infoId == pxr::UsdHydraTokens->HwUvTexture_1) { // || infoId == pxr::UsdHydraTokens->HwPtexTexture_1) {
				// look for the filename
				pxr::UsdShadeShader shaderTexture( p );
				for( const auto& input : shaderTexture.GetInputs() ) {
					auto baseName = input.GetBaseName().GetString();
					auto attrTexture = input.GetAttr();
					auto y = attrTexture.GetTypeName().GetCPPTypeName();

					if( baseName == "file" ) {
						// Retrieve the asset file.
						pxr::ArResolverContextBinder resolverContextBinder( p.GetStage()->GetPathResolverContext() );
						pxr::ArResolver& resolver = pxr::ArGetResolver();

						pxr::SdfAssetPath assetPath;
						attrTexture.Get( &assetPath, 0 );

						// FIXME: Arbitrarily replace <UDIM> with 1001. Currently unsure how to resolve this.
						if( assetPath.GetResolvedPath() == "" ) {
							std::string assetPathToCheck = assetPath.GetAssetPath();
							ReplaceAll( assetPathToCheck, "<UDIM>", "1001" );
							auto resolvedPath = resolver.Resolve( assetPathToCheck );
							assetPath = pxr::SdfAssetPath( assetPath.GetAssetPath(), resolvedPath );
						}

						if( assetPath.GetResolvedPath() != "" ) {
							// Retrieve the texture.
							auto textureAsset = resolver.OpenAsset( pxr::ArResolvedPath( assetPath.GetResolvedPath() ) );

							// check if we already have it.
							std::hash<std::string> hasher;
							int hashIdentifierPrim = hasher( std::string( textureAsset->GetBuffer().get(), textureAsset->GetSize() ) );
							// If the SHA1 hash is not found, import the texture.
							if( scene.textureCache.find( hashIdentifierPrim ) == scene.textureCache.end() ) {
								std::shared_ptr<textureVulkanite> tex( new textureVulkanite );

								tex->name = assetPath.GetResolvedPath();
								tex->textureImage = nullptr;
								createTextureImage( reinterpret_cast< const unsigned char* >( textureAsset->GetBuffer().get() ), textureAsset->GetSize(), tex->textureImage, tex->textureImageMemory, tex->mipLevels );

								if( tex->textureImage != nullptr ) {
									tex->textureImageView = createTextureImageView( tex->textureImage, tex->mipLevels, VK_FORMAT_R8G8B8A8_UNORM );
									createTextureSampler( tex->textureSampler, tex->mipLevels );

									// save the hash of the binary, and the hash of the path, for reason..... it can be multiple path to the same texture (udim maybe?)
									scene.textureCache[hashIdentifierPrim] = tex;
									scene.textureCache[hasher( assetPath.GetAssetPath() )] = tex;
								}
							} else {
								// Retrieve the texture reference from the cached tex and report it to the cache texture reference.
								scene.textureCache[hasher( assetPath.GetAssetPath() )] = scene.textureCache[hashIdentifierPrim];
							}
						} else
							spdlog::error( fmt::format( "Can't find asset with path {}", assetPath.GetAssetPath() ) );
					}
				}
			}
		}
	}
}

std::vector<objectVulkanite> loadSceneUSD( const std::string& path ) {
	{
		pxr::SdfAssetPath assetPath;
	}
	//
	auto stage = pxr::UsdStage::Open( path );
	std::vector<objectVulkanite> nodesHierarchy;
	pxr::ArResolverContextBinder resolverContextBinder( stage->GetPathResolverContext() );

	// improt all textures
	ImportTexture( stage );

	// create white texture
	{
		std::string imageName( "WhiteTex" );

		const std::shared_ptr<textureVulkanite> tex( new textureVulkanite );
		tex->name = imageName;
		auto texRC = cmrcFS.open( "textures/WhiteTex.png" );
		createTextureImage( reinterpret_cast< const unsigned char* >( texRC.cbegin() ), texRC.size(), tex->textureImage, tex->textureImageMemory, tex->mipLevels );
		tex->textureImageView = createTextureImageView( tex->textureImage, tex->mipLevels, VK_FORMAT_R8G8B8A8_UNORM );
		createTextureSampler( tex->textureSampler, tex->mipLevels );
		scene.textureCache[0] = tex;
	}

	// save sequentially all textures
	scene.textureCacheSequential.reserve( scene.textureCache.size() );

	uint32_t counterTexture = 0;
	for( auto& pair : scene.textureCache ) {
		pair.second->id = counterTexture++;
		scene.textureCacheSequential.push_back( pair.second );
	}

	// load all materials
	std::map<uint32_t, std::set<pxr::TfToken>> uvsPerMesh, uvsPerShade;
	scene.materialsCache[0] = std::make_shared<matVulkanite>();

	std::function<void( const pxr::UsdPrim& )> fLoadAllMaterials;

	fLoadAllMaterials = [&]( const pxr::UsdPrim p ) {
		auto type = p.GetTypeName();
		if( type == "Mesh" || type == "GeomSubset" ) {

			pxr::UsdGeomMesh geoUSD( p );

			std::string path = p.GetPath().GetString();

			size_t hashIdentifierPrim = 0;
			// get only the last hash identifier
			for( auto o : p.GetPrimIndex().GetNodeRange() ) {
				std::hash<std::string> hasher;
				hashIdentifierPrim = hasher(/*pxr::TfStringify(o.GetLayerStack()) + */o.GetPath().GetString() );
			}

			pxr::UsdShadeMaterialBindingAPI materialBinding( p );
			auto binding = materialBinding.GetDirectBinding();
			if( pxr::UsdShadeMaterial shadeMaterial = binding.GetMaterial() ) {
				pxr::UsdShadeShader shader = shadeMaterial.ComputeSurfaceSource();

				// if there is no shader with defaut render context, find the ONE
				if( !shader ) {
					// find the output surface with the UsdPreviewSurface (we handle this one for now)
					auto outputs = shadeMaterial.GetSurfaceOutputs();
					for( const auto& output : outputs ) {
						if( output.HasConnectedSource() ) {
							// get the source connected to the output
							auto sourceOutput = output.GetConnectedSources()[0].source;
							auto sourceShaderName = sourceOutput.GetPrim().GetName().GetString();
							if( sourceShaderName == "UsdPreviewSurface" )
								shader = pxr::UsdShadeShader( sourceOutput.GetPrim() );
						}
					}
				}

				if( shader ) {
					std::hash<std::string> hasher;
					size_t hashIdentifierMaterial = hasher( shader.GetPath().GetString() );

					// if prims already loaded
					if( hashIdentifierMaterial == 0 || scene.materialsCache.contains( hashIdentifierMaterial ) ) {
						uvsPerMesh[hashIdentifierPrim] = uvsPerShade[hashIdentifierMaterial];
					} else {
						// get the material
						auto mat = std::make_shared<matVulkanite>();
						std::set<pxr::TfToken> uvMapVarname;
						ImportMaterial( shader, uvMapVarname, *p.GetStage(), *mat );

						// save uvs map varnam per prime
						uvsPerMesh[hashIdentifierPrim] = uvMapVarname;
						uvsPerShade[hashIdentifierMaterial] = uvMapVarname;

						/*
						if (geo.skin.size())
							mat.flags |= hg::MF_EnableSkinning;
						*/

						// check double side
						bool isDoubleSided = false;
						geoUSD.GetDoubleSidedAttr().Get( &isDoubleSided );
						// if it's a geo subset check the parent 
						if( p.GetTypeName() == "GeomSubset" )
							pxr::UsdGeomMesh( p.GetParent() ).GetDoubleSidedAttr().Get( &isDoubleSided );

						mat->doubleSided = isDoubleSided;

						scene.materialsCache[hashIdentifierMaterial] = mat;
					}
				} else
					spdlog::error( "!Unexpected shader from UsdShadeShader()" );
			}
		}
		// import children
		for( auto c : p.GetChildren() ) {
			fLoadAllMaterials( c );
		}
	};

	for( const auto& p : stage->GetPseudoRoot().GetChildren() ) {
		fLoadAllMaterials( p );
	}

	std::vector<matVulkanite> materialsVector;
	materialsVector.reserve( scene.materialsCache.size() );

	uint32_t counterMaterial = 0;
	for( auto& pair : scene.materialsCache ) {
		pair.second->id = counterMaterial++;
		materialsVector.push_back( *pair.second );
	}

	createBuffer( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &scene.materialsCacheBuffer, sizeof( matVulkanite ) * materialsVector.size(),
		materialsVector.data() );

	if( scene.DRAW_RASTERIZE ) {
		// create the graphic pipeline with the right amount of textures
		createDescriptorSetLayout( scene.descriptorSetLayout );
		createGraphicsPipeline( "spv/shader.vert.spv", "spv/shader.frag.spv", scene.pipelineLayout, scene.graphicsPipeline, scene.renderPass, msaaSamples, scene.descriptorSetLayout, false );
		createGraphicsPipeline( "spv/shader.vert.spv", "spv/shader.frag.spv", scene.pipelineLayoutAlpha, scene.graphicsPipelineAlpha, scene.renderPass, msaaSamples, scene.descriptorSetLayout, true );
	}


	// load all prims
	std::function<void( const pxr::UsdPrim& )> fLoadAllPrims;

	fLoadAllPrims = [&]( const pxr::UsdPrim& p ) {
		auto type = p.GetTypeName();
		if( type == "Mesh" || type == "GeomSubset" ) {

			pxr::UsdGeomMesh geoUSD( p );

			size_t hashIdentifierPrim = 0;
			// get only the last hash identifier
			for( auto o : p.GetPrimIndex().GetNodeRange() ) {
				std::hash<std::string> hasher;
				hashIdentifierPrim = hasher(/*pxr::TfStringify(o.GetLayerStack()) + */o.GetPath().GetString() );
			}

			// if prims not already loaded
			if( hashIdentifierPrim != 0 && !scene.primsMeshCache.contains( hashIdentifierPrim ) ) {
				auto primMesh = std::make_shared<primMeshVulkanite>();

				pxr::UsdGeomSubset subsetC( p );

				ImportGeometry( geoUSD, type == "GeomSubset" ? &subsetC : nullptr, *primMesh, uvsPerMesh[hashIdentifierPrim] );
				if( primMesh->vertices.size() ) {
					createVertexBuffer( primMesh->vertices, primMesh->vertexBuffer, primMesh->vertexBufferMemory );
					createIndexBuffer( primMesh->indices, primMesh->indexBuffer, primMesh->indexBufferMemory );

					scene.primsMeshCache[hashIdentifierPrim] = primMesh;
				}
			}
		}
		// import children
		for( auto c : p.GetChildren() ) {
			fLoadAllPrims( c );
		}
	};

	for( const auto& p : stage->GetPseudoRoot().GetChildren() ) {
		fLoadAllPrims( p );
	}

	// make the big vertex cache and compute the offset for each prims
	std::vector<Vertex> allVertices;
	std::vector<uint32_t> allIndices;
	struct offsetPrim {
		uint32_t offsetVertex, offsetIndex;
	};
	std::vector<offsetPrim> offsetPrims;
	uint32_t counterPrim = 0;
	for( auto& prim : scene.primsMeshCache ) {
		prim.second->id = counterPrim;
		offsetPrims.push_back( { static_cast< uint32_t >( allVertices.size() ), static_cast< uint32_t >( allIndices.size() ) } );
		allVertices.insert( allVertices.end(), prim.second->vertices.begin(), prim.second->vertices.end() );
		allIndices.insert( allIndices.end(), prim.second->indices.begin(), prim.second->indices.end() );
		++counterPrim;
	}
	// store these buffer in vkBuffer
	VkDeviceMemory allVerticesBufferMemory;
	VkDeviceMemory allIndicesBufferMemory;
	createVertexBuffer( allVertices, scene.allVerticesBuffer, allVerticesBufferMemory );
	createIndexBuffer( allIndices, scene.allIndicesBuffer, allIndicesBufferMemory );
	createBuffer( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &scene.offsetPrimsBuffer, sizeof( offsetPrim ) * offsetPrims.size(), offsetPrims.data() );

	// import nodes.
	objectVulkanite root;

	// rotate the root if the transform up is Z
	if( UsdGeomGetStageUpAxis( stage ) == pxr::UsdGeomTokens->z )
		root.world = glm::mat4( 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f );

	for( const auto& p : stage->GetPseudoRoot().GetChildren() ) {
		auto type = p.GetTypeName();
		if( type == "Material" || type == "Shader" ) // don't import node to scene for these types
			continue;
		auto node = ImportNode( p, glm::mat4( 1 ) );

		root.children.push_back( std::move( node ) );
	}

	nodesHierarchy.push_back( std::move( root ) );
	return nodesHierarchy;
}
#endif