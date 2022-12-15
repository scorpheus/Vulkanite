#include "camera.h"

#include <GLFW/glfw3.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

//glm::mat4 camWorld(glm::lookAt(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)));
glm::mat4 camWorld(1);
static float camSpeed(1.f), camRotSpeed(0.5f);
static float pitch(0.376), yaw(-2.2), roll(0.f);
static glm::vec3 translation(-0.656056166, -0.294942170, 0.433864325);
double xMousePos, yMousePos;

void updateCamera(GLFWwindow *window, float deltaTime) {
	const glm::mat4 inverted = glm::inverse(camWorld);
	const glm::vec3 forward = normalize(glm::vec3(inverted[2]));
	const glm::vec3 right = normalize(glm::vec3(inverted[0]));
	const glm::vec3 top = normalize(glm::vec3(inverted[1]));

	float currentCamSpeed = camSpeed;

	// speed up camera
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		currentCamSpeed *= 2.f;

	// Move forward
	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		translation += forward * deltaTime * currentCamSpeed;

	// Move backward
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		translation -= forward * deltaTime * currentCamSpeed;

	// Strafe right
	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		translation -= right * deltaTime * currentCamSpeed;

	// Strafe left
	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		translation += right * deltaTime * currentCamSpeed;

	// Up
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		translation -= top * deltaTime * currentCamSpeed;

	// Down
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		translation += top * deltaTime * currentCamSpeed;


	// mouse
	double xNewMousePos, yNewMousePos;
	glfwGetCursorPos(window, &xNewMousePos, &yNewMousePos);

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		pitch += static_cast<float>(yNewMousePos - yMousePos) * camRotSpeed * deltaTime;
		yaw += static_cast<float>(xNewMousePos - xMousePos) * camRotSpeed * deltaTime;
	}

	xMousePos = xNewMousePos;
	yMousePos = yNewMousePos;

	//FPS camera:  RotationX(pitch) * RotationY(yaw)
	glm::quat qPitch = glm::angleAxis(pitch, glm::vec3(1, 0, 0));
	glm::quat qYaw = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
	glm::quat qRoll = glm::angleAxis(roll, glm::vec3(0, 0, 1));

	//For a FPS camera we can omit roll
	glm::quat orientation = qPitch * qYaw;
	orientation = glm::normalize(orientation);
	glm::mat4 rotate = glm::mat4_cast(orientation);

	glm::mat4 translate = glm::mat4(1.0f);
	translate = glm::translate(translate, translation);
	camWorld = rotate * translate; //glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}
