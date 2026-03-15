#ifndef LA_H
#define LA_H

#include <math.h>

typedef struct {
    float x, y;
} Vec2f;

Vec2f vec2(float x, float y);
Vec2f vec2_add(Vec2f a, Vec2f b);
Vec2f vec2_sub(Vec2f a, Vec2f b);
Vec2f vec2_mul(Vec2f a, float s);
Vec2f vec2_div(Vec2f a, float s);
float vec2_length(Vec2f a);

#ifdef LA_IMPL

Vec2f vec2(float x, float y) {
    return (Vec2f){x, y};
}

Vec2f vec2_add(Vec2f a, Vec2f b) {
    return vec2(a.x + b.x, a.y + b.y);
}

Vec2f vec2_sub(Vec2f a, Vec2f b) {
    return vec2(a.x - b.x, a.y - b.y);
}

Vec2f vec2_mul(Vec2f a, float s) {
    return vec2(a.x * s, a.y * s);
}

Vec2f vec2_div(Vec2f a, float s) {
    return vec2(a.x / s, a.y / s);
}

float vec2_length(Vec2f a) {
    return sqrtf(a.x * a.x + a.y * a.y);
}

#endif // LA_IMPL

#endif // LA_H
