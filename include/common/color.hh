
#pragma once

namespace color
{

inline float srgb_to_linear(float color)
{
    bool isLo = color <= 0.04045f;

    float loPart = color / 12.92f;
    float hiPart = pow((color + 0.055f) / 1.055f, (12.0f / 5.0f));
    return mix(hiPart, loPart, isLo);
}

} // namespace color
