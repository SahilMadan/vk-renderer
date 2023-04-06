#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout(push_constant) uniform constants {
	vec4 data;
	mat4 matrix;
} push_constants;

void main() {
	gl_Position = push_constants.matrix * vec4(vPosition, 1.f);
	outColor = vColor;
}