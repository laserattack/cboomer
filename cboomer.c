#include <stdio.h>
#include <stdlib.h>

#define SCREENSHOT_IMPL
#include "screenshot.h"

int main() {
    Display *display = XOpenDisplay(NULL);
    
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

