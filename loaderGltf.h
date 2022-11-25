#pragma once

#include <string>

#include "core_utils.h"

void loadSceneGltf(const std::string &scenePath, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices);
