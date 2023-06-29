#include "computeMikkTSpace.h"
#include <spdlog/spdlog.h>
#include "vertex_config.h"

#include <glm/gtx/string_cast.hpp>

#define CALC_TANGENTS_DEBUG 0


CalcTangents::CalcTangents() {
	iface.m_getNumFaces = get_num_faces;
	iface.m_getNumVerticesOfFace = get_num_vertices_of_face;

	iface.m_getNormal = get_normal;
	iface.m_getPosition = get_position;
	iface.m_getTexCoord = get_tex_coords;
	iface.m_setTSpaceBasic = set_tspace_basic;

	context.m_pInterface = &iface;
}

void CalcTangents::calc(primMeshVulkanite *mesh) {
	context.m_pUserData = mesh;

	//if (CALC_TANGENTS_DEBUG) {
	//	spdlog::debug("[CalcTangents] with Mesh: {}", mesh->name);
	//}

	genTangSpaceDefault(&this->context);
}

int CalcTangents::get_num_faces(const SMikkTSpaceContext *context) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	float f_size = (float)working_primMeshVulkanite->indices.size() / 3.f;
	int i_size = (int)working_primMeshVulkanite->indices.size() / 3;

	assert((f_size - (float)i_size) == 0.f);

	//if (CALC_TANGENTS_DEBUG) {
	//	spdlog::debug("[CalcTangents] get_num_faces: {}", i_size);
	//}

	return i_size;
}

int CalcTangents::get_num_vertices_of_face(const SMikkTSpaceContext *context, const int iFace) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	//	if (working_primMeshVulkanite->draw_mode == GL_TRIANGLES) {
	return 3;
	//	}
	//	throw std::logic_error("no vertices with less than 3 and more than 3 supported");
}

void CalcTangents::get_position(const SMikkTSpaceContext *context, float *outpos, const int iFace, const int iVert) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	const auto index = get_vertex_index(context, iFace, iVert);
	const auto &vertex = working_primMeshVulkanite->vertices[index];

	//if (CALC_TANGENTS_DEBUG) {
	//	spdlog::debug("[CalcTangents] get_position({}): {}", index, glm::to_string(vertex.pos));
	//}

	outpos[0] = vertex.pos.x;
	outpos[1] = vertex.pos.y;
	outpos[2] = vertex.pos.z;
}

void CalcTangents::get_normal(const SMikkTSpaceContext *context, float *outnormal, const int iFace, const int iVert) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	const auto index = get_vertex_index(context, iFace, iVert);
	const auto vertex = working_primMeshVulkanite->vertices[index];

	//if (CALC_TANGENTS_DEBUG) {
	//	spdlog::debug("[CalcTangents] get_normal({}): {}", index, glm::to_string(vertex.norm));
	//}

	outnormal[0] = vertex.norm.x;
	outnormal[1] = vertex.norm.y;
	outnormal[2] = vertex.norm.z;
}

void CalcTangents::get_tex_coords(const SMikkTSpaceContext *context, float *outuv, const int iFace, const int iVert) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	const auto index = get_vertex_index(context, iFace, iVert);
	const auto vertex = working_primMeshVulkanite->vertices[index];

	//if (CALC_TANGENTS_DEBUG) {
	//	spdlog::debug("[CalcTangents] get_tex_coords({}): {}", index, glm::to_string(vertex.texCoord0));
	//}

	outuv[0] = vertex.texCoord0.x;
	outuv[1] = vertex.texCoord0.y;
}

void CalcTangents::set_tspace_basic(const SMikkTSpaceContext *context, const float *tangentu, const float fSign, const int iFace, const int iVert) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	const auto index = get_vertex_index(context, iFace, iVert);
	auto *vertex = &working_primMeshVulkanite->vertices[index];

	vertex->tangent.x = tangentu[0];
	vertex->tangent.y = tangentu[1];
	vertex->tangent.z = tangentu[2];
	vertex->tangent.w = fSign;

	//if (CALC_TANGENTS_DEBUG) {
	//	spdlog::debug("[CalcTangents] set_tspace_basic({}) fSign:{}  {}", index, fSign, glm::to_string(vertex->tangent));
	//}
}

const int CalcTangents::get_vertex_index(const SMikkTSpaceContext *context, int iFace, int iVert) {
	primMeshVulkanite *working_primMeshVulkanite = static_cast<primMeshVulkanite*>(context->m_pUserData);

	const auto face_size = get_num_vertices_of_face(context, iFace);

	const auto &indices_index = (iFace * face_size) + iVert;

	const auto index = working_primMeshVulkanite->indices[indices_index];
	return index;
}
