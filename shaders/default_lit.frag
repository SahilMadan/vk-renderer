// GLSL version 4.5
#version 450

// Input
layout (location = 0) in vec3 inColor;

// Output write.
layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 1) uniform SceneData {
	vec4 frag_color; // w is for exponent.
	vec4 fog_distance; // x for min, y for max, zw unused.
	vec4 ambient_color;
	vec4 sunlight_direction; // w for sun power
	vec4 sunlight_color;
} scene_data;

void main() {
	outFragColor = vec4(inColor * scene_data.ambient_color.xyz, 1.f);
}
