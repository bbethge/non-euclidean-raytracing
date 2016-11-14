#version 130

uniform vec2 frustum = vec2(1, 1);
in vec2 vertex;
out vec2 ray_slope;

void main() {
	gl_Position = vec4(vertex, 0, 1);
	ray_slope = frustum * gl_Position.xy;
}
