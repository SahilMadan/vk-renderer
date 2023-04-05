// GLSL version 4.5
#version 450

// Input
layout (location = 0) in vec3 inColor;

// Output write.
layout (location = 0) out vec4 outFragColor;

void main() {
	// Return red.
	outFragColor = vec4(inColor, 1.f);
}
