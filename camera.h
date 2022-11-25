#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>

struct GLFWwindow;

extern glm::mat4 camWorld;
void updateCamera(GLFWwindow* window, float deltaTime);
