// Compiling this file IS the layout test: every size and offset assertion
// lives in fl_shm.h as a static_assert, so a drift makes the build fail rather
// than making a test fail. Running it re-checks the derived size arithmetic.
#include <fl_shm.h>

#include <cstdio>

int main() {
    if (fl::FlShmSizeForCapacity(FL_SHM_DEFAULT_CAPACITY) !=
        FL_SHM_RING_OFFSET + static_cast<size_t>(FL_SHM_DEFAULT_CAPACITY) * 64u) {
        std::puts("shm size arithmetic mismatch");
        return 1;
    }
    std::puts("layout ok");
    return 0;
}
