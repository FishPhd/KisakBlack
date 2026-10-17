#include "../../src/web/web_thread_policy.h"

// Core counts can be unavailable, reduced for privacy, or include efficiency
// cores. Keep the budget usable on a two-core device and bounded on large CPUs.
static_assert(Web_DefaultJobWorkers(0) == 1);
static_assert(Web_DefaultJobWorkers(1) == 1);
static_assert(Web_DefaultJobWorkers(2) == 1);
static_assert(Web_DefaultJobWorkers(4) == 2);
static_assert(Web_DefaultJobWorkers(6) == 4);
static_assert(Web_DefaultJobWorkers(8) == 4);
static_assert(Web_DefaultJobWorkers(64) == 4);

constexpr bool BoundedBudget()
{
    for (unsigned cores = 1; cores <= 256; ++cores) {
        const auto workers = Web_DefaultJobWorkers(cores);
        if (workers < 1 || workers > 4 || workers > cores) return false;
        if (cores >= 3 && workers + 2 > cores) return false;
    }
    return true;
}
static_assert(BoundedBudget());

int main() { return 0; }
