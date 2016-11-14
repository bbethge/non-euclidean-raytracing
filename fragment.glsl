#version 130

#define RESULT_NO_HIT 0u  /* Ray goes infinitely far */
#define RESULT_HIT 1u  /* Ray has hit the floor */
#define RESULT_SPHERE 2u  /* Ray has hit the window to the sphere */
#define RESULT_CYLINDER 3u  /* Ray has hit the window to the cylinder */

const float floor_level = radians(80);
const float y0 = -cos(floor_level);

// .rgb: fog color; .a: fog density, i.e. the inverse of the optical depth
uniform vec4 fog = vec4(.5, .7, .8, .25);

// Orthogonal matrix that describes the camera:
// Column 0 points rightward relative to the camera;
// Column 1 points upward relative to the camera;
// Column 2 points backward relative to the camera, like the z-axis in
//   the traditional OpenGL coördinate system;
// Column 3 is the location of the camera on the hypersphere.
uniform mat4 camera;

in vec2 ray_slope;
out vec4 frag_color;

void cast_ray_on_sphere_at_plane(
	vec4 origin, vec4 dir, vec4 plane_normal, float plane_depth,
	out vec4 result, out vec4 new_dir, out float dist, out bool hit
) {
	// 𝐫(𝑑𝑖𝑠𝑡) = 𝐨𝐫𝐢𝐠𝐢𝐧⋅cos(𝑑𝑖𝑠𝑡) + 𝐝𝐢𝐫⋅sin(𝑑𝑖𝑠𝑡)
	//　𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐫(𝑑𝑖𝑠𝑡)　=　𝑝𝑙𝑎𝑛𝑒_𝑑𝑒𝑝𝑡ℎ
	// = 𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐨𝐫𝐢𝐠𝐢𝐧⋅cos(𝑑𝑖𝑠𝑡) + 𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐝𝐢𝐫⋅sin(𝑑𝑖𝑠𝑡)
	// =　𝑎⋅cos(𝑑𝑖𝑠𝑡−𝑑𝑖𝑠𝑡₀),
	//　𝑎　=　√[(𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐨𝐫𝐢𝐠𝐢𝐧)²+(𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐝𝐢𝐫)²],
	//　𝑑𝑖𝑠𝑡₀　=　arctan(𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐝𝐢𝐫, 𝐩𝐥𝐚𝐧𝐞_𝐧𝐨𝐫𝐦𝐚𝐥∙𝐨𝐫𝐢𝐠𝐢𝐧)
	// 𝑑𝑖𝑠𝑡 = 𝑑𝑖𝑠𝑡₀ + arccos(𝑝𝑙𝑎𝑛𝑒_𝑑𝑒𝑝𝑡ℎ∕𝑎) (mod 2⋅π)
	float org_n = dot(origin, plane_normal);
	float dir_n = dot(dir, plane_normal);
	float dist0 = atan(dir_n, org_n);
	float c = plane_depth / length(vec2(org_n, dir_n));
	if (abs(c) > 1) {
		result = vec4(0);
		new_dir = vec4(0);
		dist = 0;
		hit = false;
	}
	else {
		dist = min(
			mod(dist0 - acos(c), radians(360)),
			mod(dist0 + acos(c), radians(360))
		);
		result = origin * cos(dist) + dir * sin(dist);
		new_dir = dir * cos(dist) - origin * sin(dist);
		hit = true;
	}
}

void cast_ray_on_sphere(
	vec4 origin, vec4 dir, out vec4 point_or_color, out vec4 new_dir,
	out float dist, out uint result
) {
	vec4 floor_point;
	bool hit_floor;
	cast_ray_on_sphere_at_plane(
		origin, dir, vec4(0, 1, 0, 0), y0, floor_point, new_dir,
		dist, hit_floor
	);
	vec4 window_point;
	vec4 window_dir;
	float window_dist;
	bool hit_window;
	cast_ray_on_sphere_at_plane(
		origin, dir, vec4(0, 0, 0, 1), 0, window_point, window_dir,
		window_dist, hit_window
	);
	if (hit_floor && (!hit_window || window_dist > dist)) {
		result = RESULT_HIT;
		ivec2 ipt = ivec2(256 * vec2(
			atan(floor_point.w, length(floor_point.xz)),
			atan(floor_point.z, floor_point.x)
		));
		float value = float((ipt.x^ipt.y) & 255) / 256;
		point_or_color = vec4(vec3(value), 1);
	}
	else if (hit_window && (!hit_floor || dist > window_dist)) {
		result = RESULT_CYLINDER;
		point_or_color = window_point;
		new_dir = window_dir;
		dist = window_dist;
	}
	else {
		result = RESULT_NO_HIT;
		point_or_color = vec4(0);
	}
}

void cast_ray_on_cylinder(
	vec4 origin, vec4 dir, out vec4 point_or_color, out vec4 new_dir,
	out float dist, out uint result
) {
	float mag_dir_perp = length(dir.xyz);
	vec3 unit_dir_perp = dir.xyz / mag_dir_perp;
	float t0 = atan(unit_dir_perp.y, origin.y);
	float c = y0 / length(vec2(origin.y, unit_dir_perp.y));
	if (abs(c) > 1) {
		result = RESULT_NO_HIT;
		point_or_color = vec4(0);
		new_dir = vec4(0);
		dist = 0;
	}
	else {
		result = RESULT_HIT;
		float t = min(
			mod(t0 - acos(c), radians(360)),
			mod(t0 + acos(c), radians(360))
		);
		vec4 floor_point = vec4(
			origin.xyz * cos(t) + unit_dir_perp * sin(t),
			origin.w + dir.w * t / mag_dir_perp
		);
		ivec2 ipt = ivec2(256 * vec2(
			floor_point.w, atan(floor_point.z, floor_point.x)
		));
		float value = fract(float(ipt.x^ipt.y) / 256);
		point_or_color = vec4(vec3(value), 1);
		new_dir = vec4(
			unit_dir_perp * cos(t) - origin.xyz * sin(t), dir.w
		);
		dist = t * sqrt(1 + dir.w*dir.w/(mag_dir_perp*mag_dir_perp));
	}
	if (dir.w < 0) {
		float window_dist = -origin.w / dir.w;
		if (result == RESULT_NO_HIT || window_dist < dist) {
			result = RESULT_SPHERE;
			float t = window_dist * mag_dir_perp;
			point_or_color = vec4(
				origin.xyz * cos(t) + unit_dir_perp * sin(t), 0
			);
			new_dir = vec4(
				dir.xyz * cos(t)
				- mag_dir_perp * origin.xyz * sin(t),
				dir.w
			);
			dist = window_dist;
		}
	}
}

void main() {
	if (camera[3].y < y0) {
		frag_color = vec4(0, 0, 0, 1);
		return;
	}
	uint result = camera[3].w <= 0 ? RESULT_SPHERE : RESULT_CYLINDER;
	vec4 point_or_color = camera[3];
	vec4 dir = normalize(
		ray_slope.x * camera[0] + ray_slope.y * camera[1] - camera[2]
	);
	float dist = 0;
	frag_color = vec4(0);  // Should be reassigned
	for (uint i = 0u; i < 5u; i++) {
		if (result == RESULT_SPHERE) {
			vec4 new_point_or_color;
			vec4 new_dir;
			float new_dist;
			cast_ray_on_sphere(
				point_or_color, dir, new_point_or_color,
				new_dir, new_dist, result
			);
			dist += new_dist;
			point_or_color = new_point_or_color;
			dir = new_dir;
		}
		else if (result == RESULT_CYLINDER) {
			vec4 new_point_or_color;
			vec4 new_dir;
			float new_dist;
			cast_ray_on_cylinder(
				point_or_color, dir, new_point_or_color,
				new_dir, new_dist, result
			);
			dist += new_dist;
			point_or_color = new_point_or_color;
			dir = new_dir;
		}
		else if (result == RESULT_HIT) {
			float fog_val = 1 - exp(-fog.a * dist);
			frag_color = vec4(
				mix(point_or_color.rgb, fog.rgb, fog_val), 1
			);
			break;
		}
		else if (result == RESULT_NO_HIT) {
			frag_color = vec4(fog.rgb, 1);
			break;
		}
	}
}
