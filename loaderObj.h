#pragma once

#include <string>
#include <vector>

struct Vertex;
void loadObjectObj(const std::string &scenePath, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices);
