#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SCREENSHOT_IMPL
#include "screenshot.h"

#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>

typedef struct {
    float x, y;
} Vec2f;

int main() {
    // ================ GET CURRENT DISPLAY
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open display\n");
        return 1;
    }
    
    // ================ GET WINDOW PARAMETERS
    Window root = DefaultRootWindow(display);
    XWindowAttributes root_attrs;
    XGetWindowAttributes(display, root, &root_attrs);
    int screen_width = root_attrs.width;
    int screen_height = root_attrs.height;
    printf("Screen size: %dx%d\n", screen_width, screen_height);

    XRRScreenConfiguration *screenConfig = XRRGetScreenInfo(display, root);
    int rate = XRRConfigCurrentRate(screenConfig);
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
    swa.colormap = XCreateColormap(display, root, vi->visual, AllocNone);
    swa.event_mask = ButtonPressMask | ButtonReleaseMask |
                     KeyPressMask | KeyReleaseMask |
                     PointerMotionMask | ExposureMask | ClientMessage;
    swa.override_redirect = 1;
    swa.save_under = 1;

    Window win = XCreateWindow(
        display, root,
        0, 0, screen_width, screen_height, 0,
        vi->depth, InputOutput, vi->visual,
        CWColormap | CWEventMask |
        CWOverrideRedirect | CWSaveUnder, &swa
    );

    XMapWindow(display, win);

    XClassHint classHint;
    classHint.res_name = "cboomer";
    classHint.res_class = "Cboomer";

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

    // ================ KEEP FOCUS, HANDLE ESC, RENDER TEXTURE
    XEvent event;
    int running = 1;

    Window originWindow;
    int revertToReturn;
    XGetInputFocus(display, &originWindow, &revertToReturn);

    float dt = 1.0f / rate;
    
    while (running) {
        XSetInputFocus(display, win, RevertToParent, CurrentTime);

        while (XPending(display)) {
            XNextEvent(display, &event);
            switch (event.type) {
            case KeyPress:
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == XK_Escape) {
                    running = 0;
                }
                break;
            }
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, texture);

        glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(0, 0);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(screen_width, 0);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(screen_width, screen_height);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(0, screen_height);
        glEnd();

        glXSwapBuffers(display, win);
        glFinish();
    }

    XSetInputFocus(display, originWindow, RevertToParent, CurrentTime);
    XSync(display, False);

    // ================ CLEANUP
    glDeleteTextures(1, &texture);
    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, glc);
    destroyScreenshot(display, screenshot);
    XDestroyWindow(display, win);
    XFree(vi);
    XCloseDisplay(display);

    return 0;
}
