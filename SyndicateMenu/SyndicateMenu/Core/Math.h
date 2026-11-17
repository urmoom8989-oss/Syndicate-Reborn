#pragma once
#include <cmath>
#include "../SDK/Types.h"

inline float fmin2(float a, float b) { return a < b ? a : b; }
inline float fmax2(float a, float b) { return a > b ? a : b; }

inline FQuat RotatorToQuat(const FRotator& r) {
    const float d2r = 0.017453292f / 2.f;
    float pitch = fmodf(r.Pitch, 360.f) * d2r;
    float yaw = fmodf(r.Yaw, 360.f) * d2r;
    float roll = fmodf(r.Roll, 360.f) * d2r;
    float SP = sinf(pitch), CP = cosf(pitch);
    float SY = sinf(yaw), CY = cosf(yaw);
    float SR = sinf(roll), CR = cosf(roll);
    FQuat q;
    q.x = CR * SP * SY - SR * CP * CY;
    q.y = -CR * SP * CY - SR * CP * SY;
    q.z = CR * CP * SY - SR * SP * CY;
    q.w = CR * CP * CY + SR * SP * SY;
    return q;
}
