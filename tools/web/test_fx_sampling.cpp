#include "../../src/EffectsCore/fx_sampling.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>

int main()
{
    const float curve[] = {2.0f, 4.0f, 8.0f, std::numeric_limits<float>::quiet_NaN()};
    for (float time : {0.0f, 0.25f, 0.5f, 0.99999994f, 1.0f})
    {
        const auto sample = FX_SampleInterval(2, time);
        assert(sample.index >= 0 && sample.index < 2);
        const float value = curve[sample.index] * (1.0f - sample.lerp)
            + curve[sample.index + 1] * sample.lerp;
        assert(std::isfinite(value));
        if (time == 1.0f) assert(value == 8.0f);
    }
    const auto single = FX_SampleInterval(1, 1.0f);
    assert(single.index == 0 && single.lerp == 1.0f);
}
