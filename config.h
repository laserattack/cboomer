#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    float minScale;             // Minimum allowed zoom scale (prevents zooming out too far)
    float scrollSpeed;          // Speed of zoom when scrolling or using +/- keys
    float dragFriction;         // Friction coefficient for camera movement inertia (higher = faster stop)
    float scaleFriction;        // Friction coefficient for zoom inertia (higher = faster stop)
    float initialFlDeltaRadius; // Initial delta value for flashlight radius change per Ctrl+scroll
    float velocityThreshold;    // Minimum velocity to apply inertia (prevents micro-movements)
} Config;

#ifdef CONFIG_IMPL

// YOU CAN HACK THIS VALUES
Config defaultConfig = {
    .minScale             = 0.5f,
    .scrollSpeed          = 1.5f,
    .dragFriction         = 6.0f,
    .scaleFriction        = 4.0f,
    .initialFlDeltaRadius = 250.0f,
    .velocityThreshold    = 15.0f,
};

#endif // CONFIG_IMPL
#endif // CONFIG_H
