#pragma once

struct FxSampleInterval
{
    int index;
    float lerp;
};

// A curve with N intervals has N+1 samples. At its endpoint interpolate
// the last pair, rather than reading sample N+1 with a nominal zero weight.
inline FxSampleInterval FX_SampleInterval(int count, float time)
{
    if (time <= 0.0f || count <= 0)
        return {0, 0.0f};
    const float point = float(count) * time;
    if (point >= float(count))
        return {count - 1, 1.0f};
    const int index = int(point);
    return {index, point - float(index)};
}
