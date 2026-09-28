// TODO(20260426T215403): wayland compatible

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define SCREENSHOT_IMPL
#include "screenshot.h"
#define LA_IMPL
#include "la.h"
#define CONFIG_IMPL
#include "config.h"

#include <GL/glew.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

static const char *VERTEX_SHADER_SOURCE =
    "#version 130\n"
    "in vec3       aPos;\n"
    "in vec2       aTexCoord;\n"
    "out vec2      texcoord;\n"
    "uniform vec2  camera_pos;\n"
    "uniform float camera_scale;\n"
    "uniform float camera_rotation;\n"
    "uniform vec2  window_size;\n"
    "uniform vec2  screenshot_size;\n"
    "vec3 to_world(vec3 v) {\n"
    "    vec2 ratio = vec2(\n"
    "        window_size.x / screenshot_size.x / camera_scale,\n"
    "        window_size.y / screenshot_size.y / camera_scale);\n"
    "    return vec3((v.x / screenshot_size.x * 2.0 - 1.0) / ratio.x,\n"
    "                (v.y / screenshot_size.y * 2.0 - 1.0) / ratio.y,\n"
    "                v.z);\n"
    "}\n"
    "void main() {\n"
    "    vec2 centered = aPos.xy - screenshot_size * 0.5;\n"
    "    float c = cos(camera_rotation);\n"
    "    float s = sin(camera_rotation);\n"
    "    vec2 rotated = vec2(centered.x * c - centered.y * s,\n"
    "                        centered.x * s + centered.y * c);\n"
    "    vec2 world_pos = rotated + screenshot_size * 0.5;\n"
    "    gl_Position = vec4(to_world(vec3(world_pos - camera_pos * vec2(1.0, -1.0), 0.0)), 1.0);\n"
    "    texcoord    = aTexCoord;\n"
    "}\n";

static const char *FRAGMENT_SHADER_SOURCE =
    "#version 130\n"
    "out mediump vec4  color;\n"
    "in mediump vec2   texcoord;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2      cursor_pos;\n"
    "uniform vec2      window_size;\n"
    "uniform float     fl_shadow;\n"
    "uniform float     fl_radius;\n"
    "uniform float     camera_scale;\n"
    "uniform float     fl_feather;\n"
    "uniform float     flash;\n"
    "uniform int       mirror;\n"
    "void main() {\n"
    "    vec2 uv = texcoord;\n"
    "    if (mirror == 1) uv.x = 1.0 - uv.x;\n"
    "    vec4 cursor = vec4(cursor_pos.x, window_size.y - cursor_pos.y, 0.0, 1.0);\n"
    "    float dist = length(cursor - gl_FragCoord);\n"
    "    float radius_px = fl_radius * camera_scale;\n"
    "    float inner = radius_px * (1.0 - fl_feather);\n"
    "    float outer = radius_px;\n"
    "    float alpha = smoothstep(inner, outer, dist);\n"
    "    color = mix(texture(tex, uv), vec4(0.0, 0.0, 0.0, 0.0), alpha * fl_shadow);\n"
    "    color.rgb = mix(color.rgb, vec3(1.0), flash);\n"
    "}\n";

typedef struct {
    Vec2f position;
    Vec2f velocity;
    float scale;
    float delta_scale;
    Vec2f scale_pivot;
    float rotation;
    int mirror;
} Camera;

typedef struct {
    Vec2f curr;
    Vec2f prev;
    int drag;
} Mouse;

typedef struct {
    int enabled;
    float shadow;
    float radius;
    float delta_radius; // speed of radius change
} Flashlight;

typedef struct {
    Display *display;
    Window root;
    Window window;
    XVisualInfo *visual_info;
    GLXContext gl_context;
    Window original_focus_window; // window that had focus before we stole it
    int screen_width;
    int screen_height;
    int refresh_rate;
} X11Context;

typedef struct {
    GLuint program;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    int screenshot_width;
    int screenshot_height;
} OpenGLContext;

typedef struct {
    float intensity; // current flash intensity, 0..1
} Flash;

typedef struct {
    Camera camera;
    Mouse mouse;
    Flashlight flashlight;
    Flash flash;
    float dt; // delta time (seconds since last frame)
    int running;
} State;

typedef struct {
    Config config;
    State state;
} App;

// ================ X11 STUFF

static int x11_error_handler(Display *d, XErrorEvent *ev) {
    char em[256]; // error message
    XGetErrorText(d, ev->error_code, em, sizeof(em));
    fprintf(stderr, "X11 Error: %s\n", em);
    return 0;
}

static int x11_init(X11Context *ctx) {
    ctx->display = XOpenDisplay(NULL);
    if (!ctx->display) {
        fprintf(stderr, "Failed to open display\n");
        return 0;
    }

    XSetErrorHandler(x11_error_handler);

    ctx->root = DefaultRootWindow(ctx->display);
    XWindowAttributes root_attrs;
    XGetWindowAttributes(ctx->display, ctx->root, &root_attrs);
    ctx->screen_width = root_attrs.width;
    ctx->screen_height = root_attrs.height;

    XRRScreenConfiguration *sc = XRRGetScreenInfo(ctx->display, ctx->root);
    ctx->refresh_rate = XRRConfigCurrentRate(sc);
    XRRFreeScreenConfigInfo(sc);

    printf("Screen: %dx%d @ %dHz\n",
           ctx->screen_width, ctx->screen_height, ctx->refresh_rate);

    return 1;
}

static int x11_check_glx(X11Context *ctx) {
    int glx_major, glx_minor;
    if (!glXQueryVersion(ctx->display, &glx_major, &glx_minor) ||
        (glx_major == 1 && glx_minor < 3) || (glx_major < 1)) {
        fprintf(stderr, "Invalid GLX version\n");
        return 0;
    }
    printf("GLX version: %d.%d\n", glx_major, glx_minor);
    return 1;
}

static int x11_create_window(X11Context *ctx) {
    static int attrs[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None};
    ctx->visual_info = glXChooseVisual(ctx->display, 0, attrs);
    if (!ctx->visual_info) {
        fprintf(stderr, "No appropriate visual found\n");
        return 0;
    }
    printf("Visual ID: 0x%lx\n", ctx->visual_info->visualid);

    XSetWindowAttributes swa;
    swa.colormap = XCreateColormap(ctx->display, ctx->root, ctx->visual_info->visual, AllocNone);
    swa.event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask |
                     PointerMotionMask | ExposureMask | ClientMessage;
    swa.override_redirect = 1;
    swa.save_under = 1;

    ctx->window = XCreateWindow(ctx->display, ctx->root,
                                0, 0, ctx->screen_width, ctx->screen_height, 0,
                                ctx->visual_info->depth, InputOutput,
                                ctx->visual_info->visual,
                                CWColormap | CWEventMask |
                                    CWOverrideRedirect | CWSaveUnder,
                                &swa);

    XStoreName(ctx->display, ctx->window, "cboomer");
    XClassHint class_hint = {"cboomer", "Cboomer"};
    XSetClassHint(ctx->display, ctx->window, &class_hint);

    XMapWindow(ctx->display, ctx->window);

    ctx->gl_context = glXCreateContext(ctx->display, ctx->visual_info, NULL, GL_TRUE);
    glXMakeCurrent(ctx->display, ctx->window, ctx->gl_context);

    XGetInputFocus(ctx->display, &ctx->original_focus_window, &(int){0});

    return 1;
}

static void x11_grab_focus(X11Context *ctx) {
    XSetInputFocus(ctx->display, ctx->window, RevertToParent, CurrentTime);
}

static void x11_restore_focus(X11Context *ctx) {
    XSetInputFocus(ctx->display, ctx->original_focus_window, RevertToParent, CurrentTime);
    XSync(ctx->display, False);
}

static void x11_get_window_size(X11Context *ctx, int *w, int *h) {
    XWindowAttributes wa;
    XGetWindowAttributes(ctx->display, ctx->window, &wa);
    *w = wa.width;
    *h = wa.height;
}

static void x11_cleanup(X11Context *ctx) {
    if (ctx->gl_context) {
        glXMakeCurrent(ctx->display, None, NULL);
        glXDestroyContext(ctx->display, ctx->gl_context);
    }
    if (ctx->window)
        XDestroyWindow(ctx->display, ctx->window);
    if (ctx->visual_info)
        XFree(ctx->visual_info);
    if (ctx->display)
        XCloseDisplay(ctx->display);
}

// ================ OPENGL STUFF

static GLuint opengl_compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        fprintf(stderr, "Shader error: %s\n", log);
    }
    return shader;
}

static int opengl_init(OpenGLContext *gl, Screenshot *s) {
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "GLEW error: %s\n", glewGetErrorString(err));
        return 0;
    }
    printf("GLEW: %s\n", glewGetString(GLEW_VERSION));

    glEnable(GL_TEXTURE_2D);
    gl->screenshot_width = s->image->width;
    gl->screenshot_height = s->image->height;

    return 1;
}

static void opengl_create_program(OpenGLContext *gl) {
    GLuint vertex = opengl_compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER_SOURCE);
    GLuint fragment = opengl_compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SOURCE);

    gl->program = glCreateProgram();
    glAttachShader(gl->program, vertex);
    glAttachShader(gl->program, fragment);
    glLinkProgram(gl->program);

    GLint success;
    glGetProgramiv(gl->program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(gl->program, 512, NULL, log);
        fprintf(stderr, "Program link error: %s\n", log);
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glUseProgram(gl->program);
    glUniform1i(glGetUniformLocation(gl->program, "tex"), 0);
}

static void opengl_create_texture(OpenGLContext *gl, App *app, Screenshot *s) {
    glGenTextures(1, &gl->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 s->image->width, s->image->height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, s->image->data);

    glGenerateMipmap(GL_TEXTURE_2D);
    if (app->config.texture_filter) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

static void opengl_create_geometry(OpenGLContext *gl) {
    float v[] = {
        gl->screenshot_width,
        0,
        0.0f,
        1.0f,
        1.0f,
        gl->screenshot_width,
        gl->screenshot_height,
        0.0f,
        1.0f,
        0.0f,
        0,
        gl->screenshot_height,
        0.0f,
        0.0f,
        0.0f,
        0,
        0,
        0.0f,
        0.0f,
        1.0f,
    };

    unsigned int i[] = {0, 1, 3, 1, 2, 3};

    glGenVertexArrays(1, &gl->vao);
    glGenBuffers(1, &gl->vbo);
    glGenBuffers(1, &gl->ebo);

    glBindVertexArray(gl->vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(i), i, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT,
                          GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

static void opengl_render(OpenGLContext *gl, App *app, int ww, int wh) {
    float flash = app->state.flash.intensity;
    float br = app->config.bg_r + (1.0f - app->config.bg_r) * flash;
    float bg = app->config.bg_g + (1.0f - app->config.bg_g) * flash;
    float bb = app->config.bg_b + (1.0f - app->config.bg_b) * flash;
    glClearColor(br, bg, bb, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(gl->program);
    glUniform2f(glGetUniformLocation(gl->program, "camera_pos"),
                app->state.camera.position.x, app->state.camera.position.y);
    glUniform1f(glGetUniformLocation(gl->program, "camera_scale"),
                app->state.camera.scale);
    glUniform1f(glGetUniformLocation(gl->program, "camera_rotation"),
                app->state.camera.rotation);
    glUniform1i(glGetUniformLocation(gl->program, "mirror"),
                app->state.camera.mirror);
    glUniform2f(glGetUniformLocation(gl->program, "window_size"), ww, wh);
    glUniform2f(glGetUniformLocation(gl->program, "screenshot_size"),
                gl->screenshot_width, gl->screenshot_height);
    glUniform2f(glGetUniformLocation(gl->program, "cursor_pos"),
                app->state.mouse.curr.x, app->state.mouse.curr.y);
    glUniform1f(glGetUniformLocation(gl->program, "fl_shadow"),
                app->state.flashlight.shadow);
    glUniform1f(glGetUniformLocation(gl->program, "fl_radius"),
                app->state.flashlight.radius);
    glUniform1f(glGetUniformLocation(gl->program, "fl_feather"),
                app->config.feather);
    glUniform1f(glGetUniformLocation(gl->program, "flash"),
                app->state.flash.intensity);

    glBindTexture(GL_TEXTURE_2D, gl->texture);
    glBindVertexArray(gl->vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

static void opengl_cleanup(OpenGLContext *gl) {
    if (gl->vao)
        glDeleteVertexArrays(1, &gl->vao);
    if (gl->vbo)
        glDeleteBuffers(1, &gl->vbo);
    if (gl->ebo)
        glDeleteBuffers(1, &gl->ebo);
    if (gl->program)
        glDeleteProgram(gl->program);
    if (gl->texture)
        glDeleteTextures(1, &gl->texture);
}

// ================ SAVE VIEW

static void expand_tilde(const char *in, char *out, size_t out_size) {
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (!home)
            home = ".";
        snprintf(out, out_size, "%s%s", home, in + 1);
    } else {
        snprintf(out, out_size, "%s", in);
    }
}

static void write_be32(unsigned char *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void png_write_chunk(FILE *f, const char *type,
                            const unsigned char *data, size_t len) {
    unsigned char hdr[8];
    write_be32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len > 0)
        fwrite(data, 1, len, f);

    uint32_t crc = crc32(0, (const unsigned char *)type, 4);
    if (len > 0)
        crc = crc32(crc, data, len);

    unsigned char crc_be[4];
    write_be32(crc_be, crc);
    fwrite(crc_be, 1, 4, f);
}

static void save_view(App *app, int w, int h) {
    char path[1024];
    expand_tilde(app->config.screenshot_path, path, sizeof(path));

    unsigned char *pixels = malloc((size_t)w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", path);
        free(pixels);
        return;
    }

    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);

    /* IHDR */
    unsigned char ihdr[13];
    write_be32(ihdr, (uint32_t)w);
    write_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* color type: truecolor RGB */
    ihdr[10] = 0; /* compression */
    ihdr[11] = 0; /* filter */
    ihdr[12] = 0; /* interlace */
    png_write_chunk(f, "IHDR", ihdr, 13);

    /* Build raw scanlines (filter byte 0 + RGB row), flip vertically. */
    size_t row_bytes = (size_t)w * 3;
    size_t raw_size = (row_bytes + 1) * (size_t)h;
    unsigned char *raw = malloc(raw_size);
    for (int y = 0; y < h; y++) {
        int src_y = h - 1 - y;
        raw[y * (row_bytes + 1)] = 0;
        memcpy(raw + y * (row_bytes + 1) + 1,
               pixels + (size_t)src_y * row_bytes,
               row_bytes);
    }

    uLongf compressed_size = compressBound((uLong)raw_size);
    unsigned char *compressed = malloc(compressed_size);
    compress(compressed, &compressed_size, raw, (uLong)raw_size);
    free(raw);

    png_write_chunk(f, "IDAT", compressed, (size_t)compressed_size);
    free(compressed);

    png_write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(pixels);

    printf("Saved view to %s\n", path);
}

// ================ MAIN LOGIC

static void camera_update(App *app, Vec2f ws) {

    Config *cfg = &app->config;
    Camera *c = &app->state.camera;
    Mouse *m = &app->state.mouse;
    float dt = app->state.dt;

    if (fabs(c->delta_scale) > app->config.scale_change_threshold) {
        Vec2f half = vec2_mul(ws, 0.5f);
        Vec2f sub = vec2_sub(c->scale_pivot, half);
        Vec2f p0 = vec2_div(sub, c->scale);

        c->scale += c->delta_scale * dt;
        if (c->scale < cfg->min_scale)
            c->scale = cfg->min_scale;

        Vec2f p1 = vec2_div(sub, c->scale);
        c->position = vec2_add(c->position, vec2_sub(p0, p1));
        c->delta_scale -= c->delta_scale * dt * cfg->scale_friction;
    }

    if (!m->drag && vec2_length(c->velocity) > cfg->velocity_threshold) {
        c->position = vec2_add(c->position, vec2_mul(c->velocity, dt));
        c->velocity = vec2_sub(c->velocity, vec2_mul(c->velocity, dt * cfg->drag_friction));
    }
}

static void flashlight_update(App *app) {

    Flashlight *fl = &app->state.flashlight;
    float dt = app->state.dt;

    fl->shadow = fl->enabled ? fmin(fl->shadow + app->config.fade_speed * dt, app->config.max_shadow_opacity) : fmax(fl->shadow - app->config.fade_speed * dt, 0.0f);

    if (fabs(fl->delta_radius) > app->config.radius_change_threshold) {
        fl->radius = fmax(0.0f, fl->radius + fl->delta_radius * dt);
        fl->delta_radius -= fl->delta_radius * app->config.radius_damping * dt;
    }
}

static void flash_update(App *app) {
    if (app->state.flash.intensity > 0.0f) {
        app->state.flash.intensity -= app->state.dt / app->config.flash_duration;
        if (app->state.flash.intensity < 0.0f)
            app->state.flash.intensity = 0.0f;
    }
}

static void camera_rotate(Camera *camera, float step) {
    camera->rotation = fmodf(
        camera->rotation + step,
        2.0f * (float)M_PI);
}

static Vec2f world_position(Camera *camera, Vec2f pos) {
    return vec2_div(pos, camera->scale);
}

// ================ HANDLERS

static void handle_keypress(XKeyEvent *ke, App *app, Mouse *m, int ww, int wh) {
    KeySym key = XLookupKeysym(ke, 0);
    int ctrl_pressed = (ke->state & app->config.modifier) != 0;

    if (key == app->config.key_escape) {
        app->state.running = 0;
        return;
    }

    if (key == app->config.key_flashlight) {
        app->state.flashlight.enabled = !app->state.flashlight.enabled;
        return;
    }

    if (key == app->config.key_zoom_in && ctrl_pressed && app->state.flashlight.enabled) {
        app->state.flashlight.delta_radius -= app->config.initial_delta_radius;
        return;
    }

    if (key == app->config.key_zoom_out && ctrl_pressed && app->state.flashlight.enabled) {
        app->state.flashlight.delta_radius += app->config.initial_delta_radius;
        return;
    }

    if (key == app->config.key_zoom_in) {
        app->state.camera.delta_scale += app->config.scroll_speed;
        app->state.camera.scale_pivot = m->curr;
        return;
    }

    if (key == app->config.key_zoom_out) {
        app->state.camera.delta_scale -= app->config.scroll_speed;
        app->state.camera.scale_pivot = m->curr;
        return;
    }

    if (key == app->config.key_reset) {
        app->state.camera = (Camera){.scale = 1.0f};
        return;
    }

    if (key == app->config.key_rotate_left) {
        camera_rotate(&app->state.camera, app->config.rotation_left_step);
        return;
    }

    if (key == app->config.key_rotate_right) {
        camera_rotate(&app->state.camera, -app->config.rotation_right_step);
        return;
    }

    if (key == app->config.key_mirror) {
        app->state.camera.mirror = !app->state.camera.mirror;
        return;
    }

    if (key == app->config.key_save_screenshot) {
        if (app->state.flash.intensity > 0.0f)
            return;
        app->state.flash.intensity = app->config.flash_intensity;
        save_view(app, ww, wh);
        return;
    }
}

static void handle_mousemove(XMotionEvent *motion, App *app, int rr) {
    app->state.mouse.curr = (Vec2f){.x = motion->x, .y = motion->y};

    if (app->state.mouse.drag) {
        Vec2f prev = world_position(&app->state.camera, app->state.mouse.prev);
        Vec2f cur = world_position(&app->state.camera, app->state.mouse.curr);
        app->state.camera.position = vec2_add(app->state.camera.position, vec2_sub(prev, cur));
        app->state.camera.velocity = vec2_mul(vec2_sub(prev, cur), rr);
    }

    app->state.mouse.prev = app->state.mouse.curr;
}

static void handle_buttonpress(XButtonEvent *be, App *app) {
    int ctrl_pressed = (be->state & app->config.modifier) != 0;

    if (be->button == app->config.button_drag) {
        app->state.mouse.prev = app->state.mouse.curr;
        app->state.mouse.drag = 1;
        app->state.camera.velocity = (Vec2f){.x = 0, .y = 0};
    } else if (be->button == app->config.button_zoom_in) {
        if (ctrl_pressed && app->state.flashlight.enabled) {
            app->state.flashlight.delta_radius -= app->config.initial_delta_radius;
        } else {
            app->state.camera.delta_scale += app->config.scroll_speed;
            app->state.camera.scale_pivot = app->state.mouse.curr;
        }
    } else if (be->button == app->config.button_zoom_out) {
        if (ctrl_pressed && app->state.flashlight.enabled) {
            app->state.flashlight.delta_radius += app->config.initial_delta_radius;
        } else {
            app->state.camera.delta_scale -= app->config.scroll_speed;
            app->state.camera.scale_pivot = app->state.mouse.curr;
        }
    }
}

static void handle_buttonrelease(XButtonEvent *be, App *app) {
    if (be->button == app->config.button_drag) {
        app->state.mouse.drag = 0;
    }
}

static void process_events(X11Context *x11, App *app, int ww, int wh) {
    XEvent ev;
    while (XPending(x11->display)) {
        XNextEvent(x11->display, &ev);

        switch (ev.type) {
        case KeyPress:
            handle_keypress(&ev.xkey, app, &app->state.mouse, ww, wh);
            break;
        case MotionNotify:
            handle_mousemove(&ev.xmotion, app, x11->refresh_rate);
            break;
        case ButtonPress:
            handle_buttonpress(&ev.xbutton, app);
            break;
        case ButtonRelease:
            handle_buttonrelease(&ev.xbutton, app);
            break;
        }
    }
}

// ================ INITIALIZE

static void init_app(App *app) {
    app->config = default_config;
    app->state.camera = (Camera){.scale = 1.0f};
    app->state.mouse = (Mouse){0};
    app->state.flashlight = (Flashlight){
        .enabled = 0,
        .shadow = 0.0f,
        .radius = app->config.initial_radius,
        .delta_radius = 0.0f};
    app->state.flash.intensity = 0.0f;
    app->state.dt = 0.0f;
    app->state.running = 1;
}

static void init_mouse_position(X11Context *x11, Mouse *m) {
    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(x11->display, x11->root, &root_return, &child_return,
                  &root_x, &root_y, &win_x, &win_y, &mask);
    m->curr = (Vec2f){.x = win_x, .y = win_y};
    m->prev = m->curr;
}

// ================ MAIN CYCLE

static void main_loop(X11Context *x11, OpenGLContext *gl, App *app) {
    app->state.dt = 1.0f / x11->refresh_rate;

    while (app->state.running) {
        x11_grab_focus(x11);

        int ww, wh;
        x11_get_window_size(x11, &ww, &wh);
        glViewport(0, 0, ww, wh);

        process_events(x11, app, ww, wh);

        camera_update(app, vec2(ww, wh));
        flashlight_update(app);
        flash_update(app);

        opengl_render(gl, app, ww, wh);

        glXSwapBuffers(x11->display, x11->window);
        glFinish();
    }
}

// ================ ENTRY POINT

void usage(const char *name) {
    printf("Usage: %s [OPTIONS]\n"
           "  -h, --help     Show help\n",
           name);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                usage(argv[0]);
                return 0;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return 1;
            }
        }
    }

    // init x11
    X11Context x11 = {0};
    if (!x11_init(&x11))
        return 1;
    if (!x11_check_glx(&x11)) {
        x11_cleanup(&x11);
        return 1;
    }
    if (!x11_create_window(&x11)) {
        x11_cleanup(&x11);
        return 1;
    }

    // make screenshot
    Screenshot *screenshot = new_screenshot(x11.display, x11.root);
    if (!screenshot || !screenshot->image) {
        fprintf(stderr, "Failed to take screenshot\n");
        x11_cleanup(&x11);
        return 1;
    }
    printf("Screenshot: %dx%d\n",
           screenshot->image->width, screenshot->image->height);

    // init opengl
    OpenGLContext gl = {0};
    if (!opengl_init(&gl, screenshot)) {
        destroy_screenshot(x11.display, screenshot);
        x11_cleanup(&x11);
        return 1;
    }

    // init app
    App app;
    init_app(&app);
    init_mouse_position(&x11, &app.state.mouse);

    opengl_create_program(&gl);
    opengl_create_texture(&gl, &app, screenshot);
    opengl_create_geometry(&gl);

    // main cycle
    main_loop(&x11, &gl, &app);

    // cleanup
    x11_restore_focus(&x11);
    opengl_cleanup(&gl);
    destroy_screenshot(x11.display, screenshot);
    x11_cleanup(&x11);

    return 0;
}
