#version 450

layout(location = 0) in vec4 vPosition;
layout(location = 1) in vec4 vPrevPosition;

layout(location = 0) out vec2 oVelocity;

// Entry point of the forward pipeline default uber shader (Phong and PBR)
void main() {
	vec2 a = (vPosition.xy / vPosition.w);
	vec2 b = (vPrevPosition.xy / vPrevPosition.w);
	oVelocity = (a - b) * 0.5;
}
