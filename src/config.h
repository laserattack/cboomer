#ifndef CONFIG_H
#define CONFIG_H

#include <X11/keysym.h>

typedef struct {
    // Camera settings
    float min_scale;              // Minimum allowed zoom scale
    float scroll_speed;           // Speed of zoom when scrolling or using +/- keys
    float drag_friction;          // Friction coefficient for camera movement inertia
    float scale_friction;         // Friction coefficient for zoom inertia
    float velocity_threshold;     // Minimum velocity to apply inertia
    float scale_change_threshold; // Minimum magnitude to update camera zoom (skip micro-changes)

    // Flashlight settings
    float initial_radius;          // Starting flashlight radius
    float initial_delta_radius;    // Initial delta for flashlight radius change per Ctrl+scroll
    float radius_damping;          // Radius attenuation coefficient
    float fade_speed;              // Speed of flashlight fade in/out
    float max_shadow_opacity;      // Maximum shadow opacity
    float radius_change_threshold; // Minimum magnitude to update flashlight radius (skip micro-changes)
    float feather;                 // Soft edge size as percentage of radius (0.0-0.5, e.g., 0.15 = 15%)

    // OpenGL settings
    int texture_filter;  // 0 = pixelated, 1 = smooth

    // Key bindings
    KeySym       key_escape;          // Key to quit the program
    KeySym       key_flashlight;      // Key to toggle flashlight
    KeySym       key_reset;           // Key to reset camera
    KeySym       key_zoom_in;         // Key to zoom in
    KeySym       key_zoom_out;        // Key to zoom out
    unsigned int modifier_flashlight; // Modifier for flashlight radius change (e.g., ControlMask)

    // Mouse bindings
    unsigned int button_drag;     // Mouse button for dragging
    unsigned int button_zoom_in;  // Mouse button for zoom in (scroll up)
    unsigned int button_zoom_out; // Mouse button for zoom out (scroll down)
} Config;

#ifdef CONFIG_IMPL

// YOU CAN HACK THIS VALUES
Config default_config = {
    // Camera settings
    .min_scale              = 0.5f,
    .scroll_speed           = 1.5f,
    .drag_friction          = 6.0f,
    .scale_friction         = 4.0f,
    .velocity_threshold     = 15.0f,
    .scale_change_threshold = 0.5f,

    // Flashlight settings
    .initial_radius          = 200.0f,
    .initial_delta_radius    = 250.0f,
    .radius_damping          = 10.0f,
    .fade_speed              = 6.0f,
    .max_shadow_opacity      = 0.8f,
    .radius_change_threshold = 1.0f,
    .feather                 = 0.0f,

    // OpenGL settings
    .texture_filter = 0,

    // Key bindings
    .key_escape     = XK_Escape,
    .key_flashlight = XK_2,
    .key_reset      = XK_1,
    .key_zoom_in    = XK_equal,
    .key_zoom_out   = XK_minus,

    // Ctrl = ControlMask,
    // Left Alt = Mod1Mask,
    // Shift = ShiftMask,
    // Ctrl or Shift = ControlMask | ShiftMask,
    // etc.
    .modifier_flashlight = ControlMask,

    // Mouse bindings
    .button_drag     = Button1,
    .button_zoom_in  = Button4,
    .button_zoom_out = Button5,
};

#endif // CONFIG_IMPL

#endif // CONFIG_H
