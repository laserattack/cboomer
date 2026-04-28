#ifndef CONFIG_H
#define CONFIG_H

#include <X11/keysym.h>

// TODO(20260426T214521): think about what else to put in the config

typedef struct {
    // Camera settings
    float minScale;                  // Minimum allowed zoom scale
    float scrollSpeed;               // Speed of zoom when scrolling or using +/- keys
    float dragFriction;              // Friction coefficient for camera movement inertia
    float scaleFriction;             // Friction coefficient for zoom inertia

    // Flashlight settings
    float initialFlDeltaRadius;      // Initial delta for flashlight radius change per Ctrl+scroll
    float velocityThreshold;         // Minimum velocity to apply inertia

    // Key bindings
    KeySym       keyEscape;          // Key to quit the program
    KeySym       keyFlashlight;      // Key to toggle flashlight
    KeySym       keyReset;           // Key to reset camera
    KeySym       keyZoomIn;          // Key to zoom in
    KeySym       keyZoomOut;         // Key to zoom out
    unsigned int modifierFlashlight; // Modifier for flashlight radius change (e.g., ControlMask)

    // Mouse bindings
    unsigned int buttonDrag;         // Mouse button for dragging
    unsigned int buttonZoomIn;       // Mouse button for zoom in (scroll up)
    unsigned int buttonZoomOut;      // Mouse button for zoom out (scroll down)
} Config;

#ifdef CONFIG_IMPL

// YOU CAN HACK THIS VALUES
Config defaultConfig = {
    // Camera settings
    .minScale             = 0.5f,
    .scrollSpeed          = 1.5f,
    .dragFriction         = 6.0f,
    .scaleFriction        = 4.0f,

    // Flashlight settings
    .initialFlDeltaRadius = 250.0f,
    .velocityThreshold    = 15.0f,

    // Key bindings
    .keyEscape            = XK_Escape,
    .keyFlashlight        = XK_2,
    .keyReset             = XK_1,
    .keyZoomIn            = XK_equal,
    .keyZoomOut           = XK_minus,

    // Ctrl = ControlMask,
    // Left Alt = Mod1Mask,
    // Shift = ShiftMask,
    // Ctrl or Shift = ControlMask | ShiftMask,
    // etc.
    .modifierFlashlight   = ControlMask,

    // Mouse bindings
    .buttonDrag           = Button1,
    .buttonZoomIn         = Button4,
    .buttonZoomOut        = Button5,
};

#endif // CONFIG_IMPL

#endif // CONFIG_H
