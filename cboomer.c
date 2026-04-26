// TODO(20260426T215403): wayland compatible

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCREENSHOT_IMPL
#include "screenshot.h"
#define LA_IMPL
#include "la.h"
#define CONFIG_IMPL
#include "config.h"

#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glew.h>
#include <GL/glx.h>

static const char *VERTEX_SHADER_SOURCE =
    "#version 130\n"
    "in vec3       aPos;\n"
    "in vec2       aTexCoord;\n"
    "out vec2      texcoord;\n"
    "uniform vec2  cameraPos;\n"
    "uniform float cameraScale;\n"
    "uniform vec2  windowSize;\n"
    "uniform vec2  screenshotSize;\n"
    "vec3 to_world(vec3 v) {\n"
    "    vec2 ratio = vec2(\n"
    "        windowSize.x / screenshotSize.x / cameraScale,\n"
    "        windowSize.y / screenshotSize.y / cameraScale);\n"
    "    return vec3((v.x / screenshotSize.x * 2.0 - 1.0) / ratio.x,\n"
    "                (v.y / screenshotSize.y * 2.0 - 1.0) / ratio.y,\n"
    "                v.z);\n"
    "}\n"
    "void main() {\n"
    "    gl_Position = vec4(to_world((aPos - vec3(cameraPos * vec2(1.0, -1.0), 0.0))), 1.0);\n"
    "    texcoord    = aTexCoord;\n"
    "}\n";

static const char *FRAGMENT_SHADER_SOURCE =
    "#version 130\n"
    "out mediump vec4  color;\n"
    "in mediump vec2   texcoord;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2      cursorPos;\n"
    "uniform vec2      windowSize;\n"
    "uniform float     flShadow;\n"
    "uniform float     flRadius;\n"
    "uniform float     cameraScale;\n"
    "void main() {\n"
    "    vec4 cursor = vec4(cursorPos.x, windowSize.y - cursorPos.y, 0.0, 1.0);\n"
    "    color = mix(\n"
    "        texture(tex, texcoord), vec4(0.0, 0.0, 0.0, 0.0),\n"
    "        length(cursor - gl_FragCoord) < (flRadius * cameraScale) ? 0.0 : flShadow);\n"
    "}\n";

typedef struct {
    Vec2f position;
    Vec2f velocity;
    float scale;
    float deltaScale;
    Vec2f scalePivot;
} Camera;

typedef struct {
    Vec2f curr;
    Vec2f prev;
    int   drag;
} Mouse;

typedef struct {
    int   enabled;
    float shadow;
    float radius;
    float deltaRadius; // speed of radius change
} Flashlight;

typedef struct {
    Display     *display;
    Window      root;
    Window      window;
    XVisualInfo *visualInfo;
    GLXContext  glContext;
    Window      originalFocusWindow; // window that had focus before we stole it
    int         screenWidth;
    int         screenHeight;
    int         refreshRate;
} X11Context;

typedef struct {
    GLuint program;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    int    screenshotWidth;
    int    screenshotHeight;
} OpenGLContext;

typedef struct {
    Camera     camera;
    Mouse      mouse;
    Flashlight flashlight;
    float      dt; // delta time (seconds since last frame)
    int        running;
} State;

typedef struct {
    Config config;
    State  state;
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

    ctx->root         = DefaultRootWindow(ctx->display);
    XWindowAttributes root_attrs;
    XGetWindowAttributes(ctx->display, ctx->root, &root_attrs);
    ctx->screenWidth  = root_attrs.width;
    ctx->screenHeight = root_attrs.height;

    XRRScreenConfiguration *sc = XRRGetScreenInfo(ctx->display, ctx->root);
    ctx->refreshRate           = XRRConfigCurrentRate(sc);
    XRRFreeScreenConfigInfo(sc);

    printf("Screen: %dx%d @ %dHz\n",
           ctx->screenWidth, ctx->screenHeight, ctx->refreshRate);

    return 1;
}

static int x11_check_glx(X11Context *ctx) {
    int glxMajor, glxMinor;
    if (!glXQueryVersion(ctx->display, &glxMajor, &glxMinor) ||
        (glxMajor == 1 && glxMinor < 3) || (glxMajor < 1)) {
        fprintf(stderr, "Invalid GLX version\n");
        return 0;
    }
    printf("GLX version: %d.%d\n", glxMajor, glxMinor);
    return 1;
}

static int x11_create_window(X11Context *ctx) {
    static int attrs[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None};
    ctx->visualInfo    = glXChooseVisual(ctx->display, 0, attrs);
    if (!ctx->visualInfo) {
        fprintf(stderr, "No appropriate visual found\n");
        return 0;
    }
    printf("Visual ID: 0x%lx\n", ctx->visualInfo->visualid);

    XSetWindowAttributes swa;
    swa.colormap          = XCreateColormap(ctx->display, ctx->root,
                                            ctx->visualInfo->visual, AllocNone);
    swa.event_mask        = ButtonPressMask | ButtonReleaseMask |
                            KeyPressMask | KeyReleaseMask |
                            PointerMotionMask | ExposureMask | ClientMessage;
    swa.override_redirect = 1;
    swa.save_under        = 1;

    ctx->window = XCreateWindow(ctx->display, ctx->root,
                                0, 0, ctx->screenWidth, ctx->screenHeight, 0,
                                ctx->visualInfo->depth, InputOutput,
                                ctx->visualInfo->visual,
                                CWColormap | CWEventMask |
                                CWOverrideRedirect | CWSaveUnder, &swa);

    XStoreName(ctx->display, ctx->window, "cboomer");
    XClassHint classHint = {"cboomer", "Cboomer"};
    XSetClassHint(ctx->display, ctx->window, &classHint);

    XMapWindow(ctx->display, ctx->window);

    ctx->glContext = glXCreateContext(ctx->display, ctx->visualInfo,
                                      NULL, GL_TRUE);
    glXMakeCurrent(ctx->display, ctx->window, ctx->glContext);

    XGetInputFocus(ctx->display, &ctx->originalFocusWindow, &(int){0});

    return 1;
}

static void x11_grab_focus(X11Context *ctx) {
    XSetInputFocus(ctx->display, ctx->window, RevertToParent, CurrentTime);
}

static void x11_restore_focus(X11Context *ctx) {
    XSetInputFocus(ctx->display, ctx->originalFocusWindow,
                   RevertToParent, CurrentTime);
    XSync(ctx->display, False);
}

static void x11_get_window_size(X11Context *ctx, int *w, int *h) {
    XWindowAttributes wa;
    XGetWindowAttributes(ctx->display, ctx->window, &wa);
    *w = wa.width;
    *h = wa.height;
}

static void x11_cleanup(X11Context *ctx) {
    if (ctx->glContext) {
        glXMakeCurrent(ctx->display, None, NULL);
        glXDestroyContext(ctx->display, ctx->glContext);
    }
    if (ctx->window) XDestroyWindow(ctx->display, ctx->window);
    if (ctx->visualInfo) XFree(ctx->visualInfo);
    if (ctx->display) XCloseDisplay(ctx->display);
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
    gl->screenshotWidth  = s->image->width;
    gl->screenshotHeight = s->image->height;

    return 1;
}

static void opengl_create_program(OpenGLContext *gl) {
    GLuint vertex   = opengl_compile_shader(GL_VERTEX_SHADER,
                                            VERTEX_SHADER_SOURCE);
    GLuint fragment = opengl_compile_shader(GL_FRAGMENT_SHADER,
                                            FRAGMENT_SHADER_SOURCE);

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

static void opengl_create_texture(OpenGLContext *gl, Screenshot *s) {
    glGenTextures(1, &gl->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 s->image->width, s->image->height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, s->image->data);

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

static void opengl_create_geometry(OpenGLContext *gl) {
    float v[] = {
        gl->screenshotWidth, 0, 0.0f, 1.0f, 1.0f,
        gl->screenshotWidth, gl->screenshotHeight, 0.0f, 1.0f, 0.0f,
        0, gl->screenshotHeight, 0.0f, 0.0f, 0.0f,
        0, 0, 0.0f, 0.0f, 1.0f,
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
                          GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

static void opengl_render(OpenGLContext *gl, App *app, int ww, int wh) {
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(gl->program);
    glUniform2f(glGetUniformLocation(gl->program, "cameraPos"),
                app->state.camera.position.x, app->state.camera.position.y);
    glUniform1f(glGetUniformLocation(gl->program, "cameraScale"),
                app->state.camera.scale);
    glUniform2f(glGetUniformLocation(gl->program, "windowSize"), ww, wh);
    glUniform2f(glGetUniformLocation(gl->program, "screenshotSize"),
                gl->screenshotWidth, gl->screenshotHeight);
    glUniform2f(glGetUniformLocation(gl->program, "cursorPos"),
                app->state.mouse.curr.x, app->state.mouse.curr.y);
    glUniform1f(glGetUniformLocation(gl->program, "flShadow"),
                app->state.flashlight.shadow);
    glUniform1f(glGetUniformLocation(gl->program, "flRadius"),
                app->state.flashlight.radius);

    glBindTexture(GL_TEXTURE_2D, gl->texture);
    glBindVertexArray(gl->vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

static void opengl_cleanup(OpenGLContext *gl) {
    if (gl->vao) glDeleteVertexArrays(1, &gl->vao);
    if (gl->vbo) glDeleteBuffers(1, &gl->vbo);
    if (gl->ebo) glDeleteBuffers(1, &gl->ebo);
    if (gl->program) glDeleteProgram(gl->program);
    if (gl->texture) glDeleteTextures(1, &gl->texture);
}

// ================ MAIN LOGIC

static void camera_update(Camera *c, Config cfg, float dt, Mouse m, Vec2f ws) {
    if (fabs(c->deltaScale) > 0.5f) {
        Vec2f half = vec2_mul(ws, 0.5f);
        Vec2f sub  = vec2_sub(c->scalePivot, half);
        Vec2f p0   = vec2_div(sub, c->scale);

        c->scale += c->deltaScale * dt;
        if (c->scale < cfg.minScale) c->scale = cfg.minScale;

        Vec2f p1 = vec2_div(sub, c->scale);
        c->position   = vec2_add(c->position, vec2_sub(p0, p1));
        c->deltaScale -= c->deltaScale * dt * cfg.scaleFriction;
    }

    if (!m.drag && vec2_length(c->velocity) > cfg.velocityThreshold) {
        c->position = vec2_add(c->position, vec2_mul(c->velocity, dt));
        c->velocity = vec2_sub(c->velocity,
                               vec2_mul(c->velocity, dt * cfg.dragFriction));
    }
}

static void flashlight_update(Flashlight *fl, float dt) {
    fl->shadow = fl->enabled ?
                 fmin(fl->shadow + 6.0f * dt, 0.8f) :
                 fmax(fl->shadow - 6.0f * dt, 0.0f);

    if (fabs(fl->deltaRadius) > 1.0f) {
        fl->radius      = fmax(0.0f, fl->radius + fl->deltaRadius * dt);
        fl->deltaRadius -= fl->deltaRadius * 10.0f * dt;
    }
}

static Vec2f world_position(Camera camera, Vec2f pos) {
    return vec2_div(pos, camera.scale);
}

// ================ HANDLERS

static void handle_keypress(XKeyEvent *ke, App *app, Mouse *m) {
    KeySym key = XLookupKeysym(ke, 0);

    if (key == app->config.keyEscape)
        app->state.running = 0;

    if (key == app->config.keyFlashlight)
        app->state.flashlight.enabled = !app->state.flashlight.enabled;

    if (key == app->config.keyReset)
        app->state.camera = (Camera){ .scale = 1.0f };

    if (key == app->config.keyZoomIn) {
        app->state.camera.deltaScale += app->config.scrollSpeed;
        app->state.camera.scalePivot = m->curr;
    }

    if (key == app->config.keyZoomOut) {
        app->state.camera.deltaScale -= app->config.scrollSpeed;
        app->state.camera.scalePivot = m->curr;
    }
}

static void handle_mousemove(XMotionEvent *motion, App *app, int rr) {
    app->state.mouse.curr = (Vec2f){ .x = motion->x, .y = motion->y };

    if (app->state.mouse.drag) {
        Vec2f prev = world_position(app->state.camera, app->state.mouse.prev);
        Vec2f cur  = world_position(app->state.camera, app->state.mouse.curr);
        app->state.camera.position = vec2_add(app->state.camera.position,
                                              vec2_sub(prev, cur));
        app->state.camera.velocity = vec2_mul(vec2_sub(prev, cur), rr);
    }

    app->state.mouse.prev = app->state.mouse.curr;
}

static void handle_buttonpress(XButtonEvent *be, App *app) {
    int ctrlPressed = (be->state & app->config.modifierFlashlight) != 0;

    if (be->button == app->config.buttonDrag) {
        app->state.mouse.prev      = app->state.mouse.curr;
        app->state.mouse.drag      = 1;
        app->state.camera.velocity = (Vec2f){ .x = 0, .y = 0 };
    }
    else if (be->button == app->config.buttonZoomIn) {
        if (ctrlPressed && app->state.flashlight.enabled) {
            app->state.flashlight.deltaRadius -= app->config.initialFlDeltaRadius;
        } else {
            app->state.camera.deltaScale += app->config.scrollSpeed;
            app->state.camera.scalePivot = app->state.mouse.curr;
        }
    }
    else if (be->button == app->config.buttonZoomOut) {
        if (ctrlPressed && app->state.flashlight.enabled) {
            app->state.flashlight.deltaRadius += app->config.initialFlDeltaRadius;
        } else {
            app->state.camera.deltaScale -= app->config.scrollSpeed;
            app->state.camera.scalePivot = app->state.mouse.curr;
        }
    }
}

static void handle_buttonrelease(XButtonEvent *be, App *app) {
    if (be->button == app->config.buttonDrag) {
        app->state.mouse.drag = 0;
    }
}

static void process_events(X11Context *x11, App *app) {
    XEvent ev;
    while (XPending(x11->display)) {
        XNextEvent(x11->display, &ev);

        switch (ev.type) {
            case KeyPress:
                handle_keypress(&ev.xkey, app, &app->state.mouse);
                break;
            case MotionNotify:
                handle_mousemove(&ev.xmotion, app, x11->refreshRate);
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
    app->config = defaultConfig;
    app->state.camera     = (Camera){ .scale = 1.0f };
    app->state.mouse      = (Mouse){0};
    app->state.flashlight = (Flashlight){
        .enabled     = 0,
        .shadow      = 0.0f,
        .radius      = 200.0f,
        .deltaRadius = 0.0f
    };
    app->state.dt = 0.0f;
    app->state.running = 1;
}

static void init_mouse_position(X11Context *x11, Mouse *m) {
    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(x11->display, x11->root, &root_return, &child_return,
                  &root_x, &root_y, &win_x, &win_y, &mask);
    m->curr = (Vec2f){ .x = win_x, .y = win_y };
    m->prev = m->curr;
}

// ================ MAIN CYCLE

static void main_loop(X11Context *x11, OpenGLContext *gl, App *app) {
    app->state.dt = 1.0f / x11->refreshRate;

    while (app->state.running) {
        x11_grab_focus(x11);

        int windowWidth, windowHeight;
        x11_get_window_size(x11, &windowWidth, &windowHeight);
        glViewport(0, 0, windowWidth, windowHeight);

        process_events(x11, app);

        camera_update(&app->state.camera, app->config, app->state.dt,
                      app->state.mouse, vec2(windowWidth, windowHeight));
        flashlight_update(&app->state.flashlight, app->state.dt);

        opengl_render(gl, app, windowWidth, windowHeight);

        glXSwapBuffers(x11->display, x11->window);
        glFinish();
    }
}

// ================ ENTRY POINT

void usage(const char *name) {
    printf("Usage: %s [OPTIONS]\n"
           "  -h, --help     Show help\n", name);
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
    if (!x11_init(&x11)) return 1;
    if (!x11_check_glx(&x11)) { x11_cleanup(&x11); return 1; }
    if (!x11_create_window(&x11)) { x11_cleanup(&x11); return 1; }

    // make screenshot
    Screenshot *screenshot = newScreenshot(x11.display, x11.root);
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
        destroyScreenshot(x11.display, screenshot);
        x11_cleanup(&x11);
        return 1;
    }

    opengl_create_program(&gl);
    opengl_create_texture(&gl, screenshot);
    opengl_create_geometry(&gl);

    // init app
    App app;
    init_app(&app);
    init_mouse_position(&x11, &app.state.mouse);

    // main cycle
    main_loop(&x11, &gl, &app);

    // cleanup
    x11_restore_focus(&x11);
    opengl_cleanup(&gl);
    destroyScreenshot(x11.display, screenshot);
    x11_cleanup(&x11);

    return 0;
}
