#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout (set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 projection;
	mat4 view_projection;
} camera_data;

struct ObjectData {
	mat4 model;
};

// All object matrices:
layout (set = 1, binding = 0) readonly buffer ObjectBuffer {
	ObjectData objects[];
} object_buffer;

layout(push_constant) uniform constants {
	vec4 data;
	mat4 matrix;
} push_constants;

void main() {
	mat4 model_matrix = object_buffer.objects[gl_BaseInstance].model;
	mat4 transform = camera_data.view_projection * model_matrix;
	gl_Position = transform * vec4(vPosition, 1.f);
	outColor = vColor;
}