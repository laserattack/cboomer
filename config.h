#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    float minScale;
    float scrollSpeed;
    float dragFriction;
    float scaleFriction;
    float initialFlDeltaRadius;
    float velocityThreshold;
} Config;

#ifdef CONFIG_IMPL

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
