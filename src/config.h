#ifndef CONFIG_H
#define CONFIG_H

#include <X11/keysym.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    // Camera settings
    float min_scale;              // Minimum allowed zoom scale
    float scroll_speed;           // Speed of zoom when scrolling or using +/- keys
    float drag_friction;          // Friction coefficient for camera movement inertia
    float scale_friction;         // Friction coefficient for zoom inertia
    float velocity_threshold;     // Minimum velocity to apply inertia
    float scale_change_threshold; // Minimum magnitude to update camera zoom (skip micro-changes)
    float rotation_step;          // Rotation step per key press (radians)

    // Flashlight settings
    float initial_radius;          // Starting flashlight radius
    float initial_delta_radius;    // Initial delta for flashlight radius change per Ctrl+scroll
    float radius_damping;          // Radius attenuation coefficient
    float fade_speed;              // Speed of flashlight fade in/out
    float max_shadow_opacity;      // Maximum shadow opacity
    float radius_change_threshold; // Minimum magnitude to update flashlight radius (skip micro-changes)
    float feather;                 // Soft edge size as percentage of radius (0.0-0.5, e.g., 0.15 = 15%)

    // Screenshot
    char screenshot_path[256]; // Where to save screenshot
    float flash_duration;      // Total flash duration in seconds
    float flash_intensity;     // Peak brightness (0.0 - 1.0)

    // OpenGL settings
    int texture_filter; // 0 = pixelated, 1 = smooth

    // Key bindings
    unsigned int modifier_flashlight; // Modifier for flashlight radius change (e.g., ControlMask)
    KeySym key_escape;                // Key to quit the program
    KeySym key_flashlight;            // Key to toggle flashlight
    KeySym key_reset;                 // Key to reset camera
    KeySym key_zoom_in;               // Key to zoom in
    KeySym key_zoom_out;              // Key to zoom out
    KeySym key_rotate;                // Key to rotate screenshot
    KeySym key_save_screenshot;       // Key to save current view to ~/cboomer_screenshot.png

    // Mouse bindings
    unsigned int button_drag;     // Mouse button for dragging
    unsigned int button_zoom_in;  // Mouse button for zoom in (scroll up)
    unsigned int button_zoom_out; // Mouse button for zoom out (scroll down)
} Config;

#ifdef CONFIG_IMPL

// YOU CAN HACK THIS VALUES
Config default_config = {
    // Camera settings
    .min_scale = 0.5f,
    .scroll_speed = 1.5f,
    .drag_friction = 6.0f,
    .scale_friction = 4.0f,
    .velocity_threshold = 15.0f,
    .scale_change_threshold = 0.5f,
    .rotation_step = (float)(M_PI / 2.0),

    // Flashlight settings
    .initial_radius = 200.0f,
    .initial_delta_radius = 250.0f,
    .radius_damping = 10.0f,
    .fade_speed = 6.0f,
    .max_shadow_opacity = 0.8f,
    .radius_change_threshold = 1.0f,
    .feather = 0.0f,

    // Screenshot
    .screenshot_path = "~/cboomer_screenshot.png",
    .flash_duration = 0.5f,
    .flash_intensity = 0.5f,

    // OpenGL settings
    .texture_filter = 0,

    // Key bindings

    // Ctrl = ControlMask,
    // Left Alt = Mod1Mask,
    // Shift = ShiftMask,
    // Ctrl or Shift = ControlMask | ShiftMask,
    // etc.
    .modifier_flashlight = ControlMask,

    .key_escape = XK_Escape,
    .key_flashlight = XK_2,
    .key_reset = XK_1,
    .key_zoom_in = XK_equal,
    .key_zoom_out = XK_minus,
    .key_rotate = XK_3,
    .key_save_screenshot = XK_s,

    // Mouse bindings
    .button_drag = Button1,
    .button_zoom_in = Button4,
    .button_zoom_out = Button5,
};

#endif // CONFIG_IMPL

#endif // CONFIG_H
