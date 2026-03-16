#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

// ================ SHADER SOURCES
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

static void update_flashlight(int flashlightOn, float *flShadow, float *flRadius, float *flDeltaRadius, float dt) {
    *flShadow = flashlightOn ? fmin(*flShadow + 6.0f * dt, 0.8f) : fmax(*flShadow - 6.0f * dt, 0.0f);
    if (fabs(*flDeltaRadius) > 1.0f) {
        *flRadius       = fmax(0.0f, *flRadius + *flDeltaRadius * dt);
        *flDeltaRadius -= *flDeltaRadius * 10.0f * dt;
    }
}

static Vec2f world(Camera camera, Vec2f v) {
    return vec2_div(v, camera.scale);
}

static void update_camera(Camera *camera, Config config, float dt, Mouse mouse, Vec2f windowSize) {
    if (fabs(camera->deltaScale) > 0.5f) {
        // p0 = (scalePivot - windowSize/2) / scale
        Vec2f p0 = vec2_div(vec2_sub(camera->scalePivot, vec2_mul(windowSize, 0.5f)), camera->scale);

        camera->scale = camera->scale + camera->deltaScale * dt;
        if (camera->scale < config.minScale) camera->scale = config.minScale;

        // p1 = (scalePivot - windowSize/2) / scale
        Vec2f p1 = vec2_div(vec2_sub(camera->scalePivot, vec2_mul(windowSize, 0.5f)), camera->scale);

        camera->position    = vec2_add(camera->position, vec2_sub(p0, p1));
        camera->deltaScale -= camera->deltaScale * dt * config.scaleFriction;
    }

    if (!mouse.drag && vec2_length(camera->velocity) > config.velocityThreshold) {
        camera->position = vec2_add(camera->position, vec2_mul(camera->velocity, dt));
        camera->velocity = vec2_sub(camera->velocity, vec2_mul(camera->velocity, dt * config.dragFriction));
    }
}

static GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    char  infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader compilation error: %s\n", infoLog);
    }
    return shader;
}

static GLuint createShaderProgram(const char *vertSource, const char *fragSource) {
    GLuint program        = glCreateProgram();
    GLuint vertexShader   = compileShader(GL_VERTEX_SHADER, vertSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragSource);

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    char  infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        fprintf(stderr, "Program linking error: %s\n", infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glUseProgram(program);
    return program;
}

static int xElevenErrorHandler(Display *display, XErrorEvent *errorEvent) {
    char errorMessage[256];
    XGetErrorText(display, errorEvent->error_code, errorMessage, sizeof(errorMessage));
    fprintf(stderr, "X11 Error: %s\n", errorMessage);
    return 0;
}

// TODO(20260315T234235): -h/--help flag
int main() {
    // ================ CONFIG
    Config config = defaultConfig;

    // ================ GET CURRENT DISPLAY
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open display\n");
        return 1;
    }

    // ================ SETUP X11 ERROR HANDLER
    XSetErrorHandler(xElevenErrorHandler);

    // ================ GET WINDOW PARAMETERS
    Window root = DefaultRootWindow(display);
    XWindowAttributes root_attrs;
    XGetWindowAttributes(display, root, &root_attrs);
    int screen_width  = root_attrs.width;
    int screen_height = root_attrs.height;
    printf("Screen size: %dx%d\n", screen_width, screen_height);

    XRRScreenConfiguration *screenConfig = XRRGetScreenInfo(display, root);
    int rate                             = XRRConfigCurrentRate(screenConfig);
    XRRFreeScreenConfigInfo(screenConfig);
    printf("Screen rate: %d Hz\n", rate);

    // ================ CHECK GLX VERSION
    int glxMajor, glxMinor;
    if (!glXQueryVersion(display, &glxMajor, &glxMinor) ||
        (glxMajor == 1 && glxMinor < 3) || (glxMajor < 1)) {
        fprintf(stderr, "Invalid GLX version\n");
        return 1;
    }
    printf("GLX version: %d.%d\n", glxMajor, glxMinor);

    // ================ REQUEST OPENGL CAPABILITIES
    static int attrs[] = {
        GLX_RGBA,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER,
        None
    };
    XVisualInfo *vi = glXChooseVisual(display, 0, attrs);
    if (!vi) {
        fprintf(stderr, "No appropriate visual found\n");
        return 1;
    }
    printf("Visual ID: 0x%lx\n", vi->visualid);

    // ================ CREATE AND SET UP WINDOW
    XSetWindowAttributes swa;
    swa.colormap          = XCreateColormap(display, root, vi->visual, AllocNone);
    swa.event_mask        = ButtonPressMask | ButtonReleaseMask |
                            KeyPressMask | KeyReleaseMask |
                            PointerMotionMask | ExposureMask | ClientMessage;
    swa.override_redirect = 1;
    swa.save_under        = 1;

    Window win = XCreateWindow(
        display, root,
        0, 0, screen_width, screen_height, 0,
        vi->depth, InputOutput, vi->visual,
        CWColormap | CWEventMask |
        CWOverrideRedirect | CWSaveUnder, &swa
    );

    XMapWindow(display, win);

    XClassHint classHint = {
        .res_name  = "cboomer",
        .res_class = "Cboomer",
    };

    XStoreName(display, win, "cboomer");
    XSetClassHint(display, win, &classHint);

    // ================ MAKE SCREENSHOT
    Screenshot *screenshot = newScreenshot(display, root);
    printf("Screenshot created: %dx%d\n",
           screenshot->image->width,
           screenshot->image->height);

    // ================ INITIALIZE OPENGL AND CREATE TEXTURE FROM SCREENSHOT
    GLXContext glc = glXCreateContext(display, vi, NULL, GL_TRUE);

    glXMakeCurrent(display, win, glc);

    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "GLEW initialization failed: %s\n", glewGetErrorString(err));
        return 1;
    }
    printf("GLEW initialized: %s\n", glewGetString(GLEW_VERSION));

    glViewport(0, 0, screen_width, screen_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screen_width, screen_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 screenshot->image->width,
                 screenshot->image->height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE,
                 screenshot->image->data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // ================ CREATE SHADER PROGRAM
    GLuint shaderProgram = createShaderProgram(VERTEX_SHADER_SOURCE, FRAGMENT_SHADER_SOURCE);

    // ================ CREATE VAO/VBO FOR RECTANGLE
    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    float w = screenshot->image->width;
    float h = screenshot->image->height;

    float vertices[] = {
        w, 0, 0.0f, 1.0f, 1.0f,
        w, h, 0.0f, 1.0f, 0.0f,
        0, h, 0.0f, 0.0f, 0.0f,
        0, 0, 0.0f, 0.0f, 1.0f
    };

    unsigned int indices[] = { 0, 1, 3, 1, 2, 3 };

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Uniform for texture
    glUniform1i(glGetUniformLocation(shaderProgram, "tex"), 0);

    // ================ FLASHLIGHT BASE VARIABLES
    int   flashlightOn  = 0;
    float flShadow      = 0.0f;
    float flRadius      = 200.0f;
    float flDeltaRadius = 0.0f;

    // ================ PREPARING THE CURSOR AND CAMERA
    Camera camera = { .scale = 1.0f };
    Mouse mouse = {0};
    // Get cursor position
    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(display, root, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask);
    mouse.curr = (Vec2f){ .x = win_x, .y = win_y };
    
    // ================ MAIN CYCLE: KEEP FOCUS, HANDLE ESC, RENDER TEXTURE
    XEvent event;
    int running = 1;

    Window originWindow;
    int revertToReturn;
    XGetInputFocus(display, &originWindow, &revertToReturn);

    float dt = 1.0f / rate;
    
    while (running) {
        XSetInputFocus(display, win, RevertToParent, CurrentTime);

        update_flashlight(flashlightOn, &flShadow, &flRadius, &flDeltaRadius, dt);
        update_camera(&camera, config, dt, mouse, vec2(screen_width, screen_height));

        while (XPending(display)) {
            XNextEvent(display, &event);
            switch (event.type) {
            case KeyPress:
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == config.keyEscape)     { running = 0;                        }
                if (key == config.keyFlashlight) { flashlightOn = !flashlightOn;       }
                if (key == config.keyReset)      { camera = (Camera){ .scale = 1.0f }; }
                if (key == config.keyZoomIn) {
                    camera.deltaScale += config.scrollSpeed;
                    camera.scalePivot  = mouse.curr;
                }
                if (key == config.keyZoomOut) {
                    camera.deltaScale -= config.scrollSpeed;
                    camera.scalePivot  = mouse.curr;
                }
                break;

            case MotionNotify:
                mouse.curr = (Vec2f){ .x = event.xmotion.x, .y = event.xmotion.y };

                if (mouse.drag) {
                    Vec2f worldPrev = world(camera, mouse.prev);
                    Vec2f worldCurr = world(camera, mouse.curr);
                    camera.position = vec2_add(camera.position, vec2_sub(worldPrev, worldCurr));
                    camera.velocity = vec2_mul(vec2_sub(worldPrev, worldCurr), rate);
                }

                mouse.prev = mouse.curr;
                break;

            case ButtonPress:
                int ctrlPressed = (event.xbutton.state & config.modifierFlashlight) != 0;

                if (event.xbutton.button == config.buttonDrag) {
                    mouse.prev        = mouse.curr;
                    mouse.drag        = 1;
                    camera.velocity   = (Vec2f){ .x = 0, .y = 0 };
                } else if (event.xbutton.button == config.buttonZoomIn) {
                    if (ctrlPressed && flashlightOn) {
                        flDeltaRadius -= config.initialFlDeltaRadius;
                    } else {
                        camera.deltaScale += config.scrollSpeed;
                        camera.scalePivot  = mouse.curr;
                    }
                } else if (event.xbutton.button == config.buttonZoomOut) {
                    if (ctrlPressed && flashlightOn) {
                        flDeltaRadius += config.initialFlDeltaRadius;
                    } else {
                        camera.deltaScale -= config.scrollSpeed;
                        camera.scalePivot  = mouse.curr;
                    }
                }
                break;

            case ButtonRelease:
                if (event.xbutton.button == config.buttonDrag) mouse.drag = 0;
                break;
            }
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Set uniforms
        glUniform2f(glGetUniformLocation(shaderProgram, "cameraPos"), camera.position.x, camera.position.y);
        glUniform1f(glGetUniformLocation(shaderProgram, "cameraScale"), camera.scale);
        glUniform2f(glGetUniformLocation(shaderProgram, "windowSize"), screen_width, screen_height);
        glUniform2f(glGetUniformLocation(shaderProgram, "screenshotSize"),
                                         screenshot->image->width, screenshot->image->height);
        glUniform2f(glGetUniformLocation(shaderProgram, "cursorPos"), mouse.curr.x, mouse.curr.y);
        glUniform1f(glGetUniformLocation(shaderProgram, "flShadow"), flShadow);
        glUniform1f(glGetUniformLocation(shaderProgram, "flRadius"), flRadius);

        glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glXSwapBuffers(display, win);
        glFinish();
    }

    XSetInputFocus(display, originWindow, RevertToParent, CurrentTime);
    XSync(display, False);

    // ================ CLEANUP
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &texture);
    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, glc);
    destroyScreenshot(display, screenshot);
    XDestroyWindow(display, win);
    XFree(vi);
    XCloseDisplay(display);

    return 0;
}
