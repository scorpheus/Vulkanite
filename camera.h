#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>

struct GLFWwindow;

extern glm::vec2 jitterCam;
extern glm::mat4 camWorld;
extern float pitch, yaw, roll;
extern glm::vec3 translation;
void updateCamWorld(glm::mat4 world);
void updateJitter(glm::vec2 &jitterCam, uint32_t frameIndex);
void updateCamera(GLFWwindow *window, float deltaTime);
