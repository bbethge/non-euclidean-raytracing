#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_cblas.h>
#include <gsl/gsl_minmax.h>
#include "SDL.h"
#include "SDL_syswm.h"
#include <GL/glew.h>

#define FRAME_TIME_MS 20
#define MOVE_SPEED_PER_FRAME (0.2 * FRAME_TIME_MS / 1000)

extern const char vertex_source[];
extern const char fragment_source[];

bool handle_event(
	const SDL_Event *event, GLfloat *camera_basis, float mouse_sensitivity
);
void handle_keyboard_state(GLfloat *camera_basis);
void move_camera_on_sphere(
	GLfloat vx, GLfloat vz, GLfloat dist, GLfloat *camera_basis
);
void move_camera_on_cylinder(
	GLfloat vx, GLfloat vz, GLfloat dist, GLfloat *camera_basis
);
void fix_camera_basis(GLfloat *camera_basis);
void render(SDL_Window *window, GLuint program, const GLfloat *camera_basis);
GLuint compile_shader(GLenum type, const char *source, SDL_Window *window);
void show_error(
	SDL_MessageBoxFlags flag, const char *title, SDL_Window *window,
	const char *format, ...
);

const GLint vertices[] = { -1, -1, -1,  1,  1, -1,  1,  1 };

int main(int argc, char *argv[]) {
	GLfloat camera_basis[16] = {
		-1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, -1
	};
	Uint32 prev_time;
	Uint32 time;

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", NULL,
			"Could not initialize SDL: %s", SDL_GetError()
		);
		return 1;
	}
	atexit(SDL_Quit);
	SDL_GL_SetAttribute(
		SDL_GL_CONTEXT_PROFILE_MASK,
		SDL_GL_CONTEXT_PROFILE_CORE
	);
	/* SDL seems to default to GL 2.1 on my system.  The glxinfo
	 * command reports GL 3.0 for the compatibility profile and
	 * GL 3.1 for the core profile, but specifying 3.1 here doesn’t
	 * work even with the core profile.
	 */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_Window *window = SDL_CreateWindow(
		"Non-Euclidean Raytracing",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP
	);
	if (window == NULL) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", NULL,
			"Could not create a window: %s", SDL_GetError()
		);
		return 1;
	}
	SDL_SetRelativeMouseMode(SDL_TRUE);
	int window_width, window_height;
	SDL_GetWindowSize(window, &window_width, &window_height);
	float mouse_sensitivity =
#ifdef WORKAROUND_RAW_MOUSE_EVENTS
		/* FIXME: On X11, SDL uses raw events for relative mouse mode
		 * regardless of the hint, so we have no way of knowing
		 * the units of mouse_sensitivity.
		 */
		mouse_sensitivity = 0.00025f;
#else
		mouse_sensitivity = 4.0f / window_height;
#endif
	SDL_GLContext context = SDL_GL_CreateContext(window);
	if (context == NULL) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Could not create an OpenGL context: %s",
			SDL_GetError()
		);
		return 1;
	}
	GLenum glew_error = glewInit();
	if (glew_error != GLEW_OK) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Could not initialize the GLEW library: %s",
			glewGetErrorString(glew_error)
		);
		return 1;
	}
	if (!GLEW_VERSION_3_0) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"OpenGL 3.0 is not supported"
		);
		return 1;
	}
	glViewport(0, 0, window_width, window_height);
	GLuint vertex_shader = compile_shader(
		GL_VERTEX_SHADER, vertex_source, window
	);
	if (vertex_shader == 0) return 1;
	GLuint fragment_shader = compile_shader(
		GL_FRAGMENT_SHADER, fragment_source, window
	);
	if (fragment_shader == 0) return 1;
	GLuint program = glCreateProgram();
	if (program == 0) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Could not create a shader program (error 0x%x)",
			glGetError()
		);
		return 1;
	}
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (!link_status) {
		GLint log_length;
		char *log;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
		log = malloc(log_length);
		glGetProgramInfoLog(program, log_length, NULL, log);
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Shader program linking failed: %s", log
		);
		free(log);
		return 1;
	}
	GLint frustum_location = glGetUniformLocation(program, "frustum");
	glUseProgram(program);
	glUniform2f(frustum_location, 0.5 * window_width / window_height, 0.5);
	GLint vertex_location = glGetAttribLocation(program, "vertex");
	if (vertex_location == -1) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Vertex attribute “vertex” should be used in the "
			"vertex shader but isn’t"
		);
		return 1;
	}
	GLuint vertex_buffer;
	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(
		GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW
	);
	glVertexAttribPointer(vertex_location, 2, GL_INT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(vertex_location);
	prev_time = SDL_GetTicks();
	render(window, program, camera_basis);
	for (;;) {
		SDL_Event event;
		bool terminate;
		while(SDL_PollEvent(&event)) {
			terminate = handle_event(
				&event, camera_basis, mouse_sensitivity
			);
			if (terminate) break;
		}
		if (terminate) break;
		handle_keyboard_state(camera_basis);
		time = SDL_GetTicks();
		if (time - prev_time < FRAME_TIME_MS) {
			SDL_Delay(FRAME_TIME_MS - time + prev_time);
		}
		prev_time = time;
		render(window, program, camera_basis);
	}
	glDeleteBuffers(1, &vertex_buffer);
	glDeleteProgram(program);
	glDeleteShader(fragment_shader);
	glDeleteShader(vertex_shader);
	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	return 0;
}

/* Store dest*src into dest, where dest and src are 4×4 column-major
 * matrices.
 */
void in_place_multiply_matrix(float *dest, const float *src) {
	float new_value[16];
	cblas_sgemm(
		CblasColMajor, CblasNoTrans, CblasNoTrans, 4, 4, 4,
		1.0, dest, 4, src, 4, 0.0, new_value, 4
	);
	memcpy(dest, new_value, 16 * sizeof(float));
}

/* Handle an event, updating camera_basis as necessary and returning
 * true if the program should terminate.
 */
bool handle_event(
	const SDL_Event *event, float *camera_basis, float mouse_sensitivity
) {
	if (
		event->type == SDL_QUIT
		|| event->type == SDL_KEYDOWN
		&& event->key.keysym.scancode == SDL_SCANCODE_ESCAPE
	) {
		return true;
	}
	if (event->type == SDL_MOUSEMOTION) {
		GLfloat cx = cosf(mouse_sensitivity * event->motion.xrel);
		GLfloat sx = sinf(mouse_sensitivity * event->motion.xrel);
		GLfloat cy = cosf(mouse_sensitivity * event->motion.yrel);
		GLfloat sy = sinf(mouse_sensitivity * event->motion.yrel);
		GLfloat rotx[16] = {
			cx, 0, sx, 0,  0, 1, 0, 0,  -sx, 0, cx, 0,  0, 0, 0, 1
		};
		GLfloat roty[16] = {
			1, 0, 0, 0,  0, cy, -sy, 0,  0, sy, cy, 0,  0, 0, 0, 1
		};
		GLfloat old_w = camera_basis[15];
		if (old_w > 0) camera_basis[15] = 0.0f;
		in_place_multiply_matrix(camera_basis, rotx);
		in_place_multiply_matrix(camera_basis, roty);
		if (old_w > 0) camera_basis[15] = old_w;
		fix_camera_basis(camera_basis);
	}
	return false;
}

void handle_keyboard_state(GLfloat *camera_basis) {
	GLfloat vx = 0, vz = 0;
	GLfloat len_v;
	const Uint8 *kbd = SDL_GetKeyboardState(NULL);
	if (kbd[SDL_SCANCODE_A]) vx -= 1;
	if (kbd[SDL_SCANCODE_D]) vx += 1;
	if (kbd[SDL_SCANCODE_S]) vz += 1;
	if (kbd[SDL_SCANCODE_W]) vz -= 1;
	len_v = hypotf(vx, vz);
	if (len_v != 0) {
		vx /= len_v;
		vz /= len_v;
		if (camera_basis[15] <= 0) {
			move_camera_on_sphere(
				vx, vz, MOVE_SPEED_PER_FRAME, camera_basis
			);
		}
		else {
			move_camera_on_cylinder(
				vx, vz, MOVE_SPEED_PER_FRAME, camera_basis
			);
		}
		fix_camera_basis(camera_basis);
	}
}

void move_camera_on_sphere(
	GLfloat vx, GLfloat vz, GLfloat dist, GLfloat *camera_basis
) {
	GLfloat dir_w = vx * camera_basis[3] + vz * camera_basis[11];
	GLfloat max_dist = 4*acosf(0) - GSL_MAX(
		fmodf(
			5*acosf(0) - atan2f(dir_w, camera_basis[15]),
			4*acosf(0)
		),
		fmodf(
			3*acosf(0) - atan2f(dir_w, camera_basis[15]),
			4*acosf(0)
		)
	);
	GLfloat dist_ = GSL_MIN(max_dist, dist);
	GLfloat c = cosf(dist_), s = sinf(dist_);
	/* Actually in column-major order, so mentally transpose. */
	GLfloat rot[16] = {
		vx*vx*c + vz*vz, 0, vx*vz*(c-1),     -vx*s,
		0,               1, 0,               0,
		vx*vz*(c-1),     0, vz*vz*c + vx*vx, -vz*s,
		vx*s,            0, vz*s,            c
	};
	in_place_multiply_matrix(camera_basis, rot);
	if (dist_ < dist) {
		move_camera_on_cylinder(vx, vz, dist - dist_, camera_basis);
	}
}

void move_camera_on_cylinder(
	GLfloat vx, GLfloat vz, GLfloat dist, GLfloat *camera_basis
) {
	/* dir = vx⋅𝐜₀ + vy⋅𝐜₂ */
	GLfloat dir[4] = { 0 };
	cblas_saxpy(4, vx, &camera_basis[0], 1, dir, 1);
	cblas_saxpy(4, vz, &camera_basis[8], 1, dir, 1);
	GLfloat max_dist =
		dir[3] < 0.0f ? -camera_basis[15] / dir[3] : HUGE_VALF;
	GLfloat dist_ = GSL_MIN(max_dist, dist);
	GLfloat angle = dist_ * sqrtf(1.0f - dir[3]*dir[3]);
	GLfloat c = cosf(angle), s = sinf(angle);
	/* dir_perp = 𝐯 = (dir − dir.w⋅𝐰) ∕ |dir − dir.w⋅𝐰| */
	GLfloat dir_perp[3] = { 0 };
	cblas_saxpy(3, 1.0f / cblas_snrm2(3, dir, 1), dir, 1, dir_perp, 1);
	/* lrot = 𝟙 + (𝐜₃⊗𝐜₃+𝐯⊗𝐯)⋅(c−1) + (𝐯⊗𝐜₃−𝐜₃⊗𝐯)⋅s */
	GLfloat lrot[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
	cblas_sger(
		CblasColMajor, 3, 3, c - 1.0f, &camera_basis[12], 1,
		&camera_basis[12], 1, lrot, 3
	);
	cblas_sger(
		CblasColMajor, 3, 3, c - 1.0f, dir_perp, 1, dir_perp, 1,
		lrot, 3
	);
	cblas_sger(
		CblasColMajor, 3, 3, s, dir_perp, 1, &camera_basis[12], 1,
		lrot, 3
	);
	cblas_sger(
		CblasColMajor, 3, 3, -s, &camera_basis[12], 1, dir_perp, 1,
		lrot, 3
	);
	GLfloat new_camera_basis[16] = { 0 };
	cblas_sgemm(
		CblasColMajor, CblasNoTrans, CblasNoTrans, 3, 4, 3, 1.0f,
		lrot, 3, camera_basis, 4, 0.0f, new_camera_basis, 4
	);
	cblas_scopy(4, &new_camera_basis[0], 4, &camera_basis[0], 4);
	cblas_scopy(4, &new_camera_basis[1], 4, &camera_basis[1], 4);
	cblas_scopy(4, &new_camera_basis[2], 4, &camera_basis[2], 4);
	camera_basis[15] += dist_ * dir[3];
	if (dist_ < dist) {
		move_camera_on_sphere(vx, vz, dist - dist_, camera_basis);
	}
}

void fix_camera_basis(GLfloat *camera_basis) {
	GLfloat dots[3];
	GLfloat old_w = camera_basis[15];
	if (old_w > 0.0f) camera_basis[15] = 0.0f;
	cblas_sscal(
		4, 1.0f / cblas_snrm2(4, &camera_basis[0], 1),
		&camera_basis[0], 1
	);
	cblas_saxpy(
		4, -cblas_sdot(4, &camera_basis[0], 1, &camera_basis[4], 1),
		&camera_basis[0], 1, &camera_basis[4], 1
	);
	cblas_sscal(
		4, 1.0f / cblas_snrm2(4, &camera_basis[4], 1),
		&camera_basis[4], 1
	);
	cblas_sgemv(
		CblasColMajor, CblasTrans, 4, 2, 1.0f,
		&camera_basis[0], 4, &camera_basis[8], 1, 0.0f, dots, 1
	);
	cblas_sgemv(
		CblasColMajor, CblasNoTrans, 4, 2, -1.0f,
		&camera_basis[0], 4, dots, 1, 1.0f, &camera_basis[8], 1
	);
	cblas_sscal(
		4, 1.0f / cblas_snrm2(4, &camera_basis[8], 1),
		&camera_basis[8], 1
	);
	cblas_sgemv(
		CblasColMajor, CblasTrans, 4, 3, 1.0f,
		&camera_basis[0], 4, &camera_basis[12], 1, 0.0f, dots,1
	);
	cblas_sgemv(
		CblasColMajor, CblasNoTrans, 4, 3, -1.0f,
		&camera_basis[0], 4, dots, 1, 1.0f, &camera_basis[12],1
	);
	cblas_sscal(
		4, 1.0f / cblas_snrm2(4, &camera_basis[12], 1),
		&camera_basis[12], 1
	);
	if (old_w > 0.0f) camera_basis[15] = old_w;
}

void render(SDL_Window *window, GLuint program, const GLfloat *camera_basis) {
	GLint camera_location = glGetUniformLocation(program, "camera");
	glUniformMatrix4fv(camera_location, 1, GL_FALSE, camera_basis);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	SDL_GL_SwapWindow(window);
}

GLuint compile_shader(GLenum type, const char *source, SDL_Window *window) {
	const char *type_str =
		type == GL_FRAGMENT_SHADER ? "fragment"
		: type == GL_VERTEX_SHADER ? "vertex" : "";
	GLint compile_status;
	GLuint shader = glCreateShader(type);
	if (shader == 0) {
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Could not create a %s shader", type_str
		);
		return 0;
	}
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (!compile_status) {
		GLint log_length;
		GLchar *log;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
		log = malloc(log_length * sizeof(GLchar));
		glGetShaderInfoLog(shader, log_length, NULL, log);
		show_error(
			SDL_MESSAGEBOX_ERROR, "Error", window,
			"Compilation of the %s shader failed: %s",
			type_str, log
		);
		free(log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

void show_error(
	SDL_MessageBoxFlags flag, const char *title, SDL_Window *window,
	const char *format, ...
) {
	va_list args;
	int len;
	char *msg;
	va_start(args, format);
	len = vsnprintf(NULL, 0, format, args);
	va_end(args);
	msg = malloc(len + 1);
	va_start(args, format);
	vsnprintf(msg, len + 1, format, args);
	va_end(args);
	if (SDL_ShowSimpleMessageBox(flag, title, msg, window) < 0) {
		fputs(title, stderr);
		fputs(": ", stderr);
		fputs(msg, stderr);
		fputc('\n', stderr);
	}
	free(msg);
}
