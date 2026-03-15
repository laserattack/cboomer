#include <stdlib.h>
#include <stdio.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

typedef struct {
    XImage *image;
    XShmSegmentInfo *shminfo;
} Screenshot;

Screenshot* newScreenshot(Display *display, Window window) {
    Screenshot *result = malloc(sizeof(Screenshot));

    XWindowAttributes attributes;
    XGetWindowAttributes(display, window, &attributes);
    
    result->shminfo = malloc(sizeof(XShmSegmentInfo));

    int screen = DefaultScreen(display);
    
    result->image = XShmCreateImage(
        display,
        DefaultVisual(display, screen),
        DefaultDepthOfScreen(ScreenOfDisplay(display, screen)),
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
    
    result->shminfo->shmaddr = (char*)shmat(result->shminfo->shmid,
                                            0, 0);
    result->image->data = result->shminfo->shmaddr;
    result->shminfo->readOnly = False;
    
    XShmAttach(display, result->shminfo);
    
    XShmGetImage(display, window, result->image, 0, 0, AllPlanes);
    
    return result;
}

void destroyScreenshot(Display *display, Screenshot *screenshot) {
    if (!screenshot) return;

    XSync(display, False);
    XShmDetach(display, screenshot->shminfo);
    XDestroyImage(screenshot->image);
    shmdt(screenshot->shminfo->shmaddr);
    shmctl(screenshot->shminfo->shmid, IPC_RMID, 0);
    free(screenshot->shminfo);
    free(screenshot);
}

void saveToPPM(XImage *image, const char *filePath) {
    FILE *f = fopen(filePath, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", filePath);
        return;
    }

    fprintf(f, "P6\n");
    fprintf(f, "%d %d\n", image->width, image->height);
    fprintf(f, "255\n");

    for (int i = 0; i < image->width * image->height; i++) {
        fputc(image->data[i * 4 + 2], f);
        fputc(image->data[i * 4 + 1], f);
        fputc(image->data[i * 4 + 0], f);
    }
    
    fclose(f);
}

int main() {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }
    
    Window root = DefaultRootWindow(display);
    
    Screenshot *screenshot = newScreenshot(display, root);
    if (screenshot) {
        printf("Screenshot created: %dx%d\n",
        screenshot->image->width, screenshot->image->height);

        saveToPPM(screenshot->image, "screenshot.ppm");
        
        destroyScreenshot(display, screenshot);
    } else {
        fprintf(stderr, "Failed to create screenshot\n");
    }
    
    XCloseDisplay(display);
    return 0;
}
