// Compiling this file IS the layout test: every size and offset assertion
// lives in fl_shm.h as a static_assert, so a drift makes the build fail rather
// than making a test fail. Running it re-checks the derived size arithmetic.
#include <cstdio>
#include <cstring>
#include <fl_shm.h>

int main() {
    // FL_BUILD_ID must EXIST and must FIT. It had no producer at all, and a
    // buildId nobody writes turns 07_IPC's refuse-to-attach-on-mismatch into a
    // comparison of "" with "" — a gate that cannot fail, wearing a version-skew
    // costume. Asserted here because this binary is the one that exists purely
    // to check the shm contract.
#ifndef FL_BUILD_ID
    std::puts("FL_BUILD_ID is not defined — FlShmHandshake::buildId would have no producer");
    return 1;
#else
    if (std::strlen(FL_BUILD_ID) == 0) {
        std::puts("FL_BUILD_ID is empty — an empty build id makes the mismatch check vacuous");
        return 1;
    }
    if (std::strlen(FL_BUILD_ID) >= sizeof(fl::FlShmHandshake::buildId)) {
        std::puts("FL_BUILD_ID does not fit in FlShmHandshake::buildId");
        return 1;
    }
    std::printf("build id: %s\n", FL_BUILD_ID);
#endif

    if (fl::FlShmSizeForCapacity(FL_SHM_DEFAULT_CAPACITY) !=
        FL_SHM_RING_OFFSET + static_cast<size_t>(FL_SHM_DEFAULT_CAPACITY) * 64u) {
        std::puts("shm size arithmetic mismatch");
        return 1;
    }
    std::puts("layout ok");
    return 0;
}
