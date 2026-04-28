#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef USE_XSHM
#include <X11/extensions/XShm.h>
#endif

typedef struct {
    XImage          *image;
#ifdef USE_XSHM
    XShmSegmentInfo *shminfo;
#endif
} Screenshot;

Screenshot *newScreenshot(Display *display, Window window);
void       destroyScreenshot(Display *display, Screenshot *screenshot);

// TODO(20260315T135543): Maybe add error checking

#ifdef SCREENSHOT_IMPL

#include <stdlib.h>

#ifdef USE_XSHM
#include <sys/shm.h>
#endif

#define UNUSED(x) (void)(x)

Screenshot *newScreenshot(Display *d, Window w) {
    Screenshot *result = malloc(sizeof(Screenshot));

    XWindowAttributes attributes;
    XGetWindowAttributes(d, w, &attributes);

#ifdef USE_XSHM
    result->shminfo = malloc(sizeof(XShmSegmentInfo));

    int screen = DefaultScreen(d);

    result->image = XShmCreateImage(
        d,
        DefaultVisual(d, screen),
        DefaultDepthOfScreen(ScreenOfDisplay(d, screen)),
        ZPixmap,
        NULL,
        result->shminfo,
        attributes.width,
        attributes.height
    );

    result->shminfo->shmid = shmget(
        IPC_PRIVATE,
        result->image->bytes_per_line * result->image->height,
        IPC_CREAT | 0777
    );

    result->shminfo->shmaddr  = (char*)shmat(result->shminfo->shmid, 0, 0);
    result->image->data       = result->shminfo->shmaddr;
    result->shminfo->readOnly = False;

    XShmAttach(d, result->shminfo);

    XShmGetImage(d, w, result->image, 0, 0, AllPlanes);
#else // USE_XSHM
    result->image = XGetImage(d, w, 0, 0,
                              attributes.width, attributes.height,
                              AllPlanes, ZPixmap);
#endif // USE_XSHM

    return result;
}

void destroyScreenshot(Display *d, Screenshot *s) {
    if (!s) return;

#ifdef USE_XSHM
    XSync(d, False);
    XShmDetach(d, s->shminfo);
    XDestroyImage(s->image);
    shmdt(s->shminfo->shmaddr);
    shmctl(s->shminfo->shmid, IPC_RMID, 0);
    free(s->shminfo);
#else
    UNUSED(d);
    XDestroyImage(s->image);
#endif
    free(s);
}

#endif // SCREENSHOT_IMPL

#endif // SCREENSHOT_H
