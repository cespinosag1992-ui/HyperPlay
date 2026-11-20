#include <cstdio>
#include <cstdint>
#include "gfx/FsrCasSpv.h"

int main() {
    // Verificación de FsrCasSpv
    static_assert(hyperplay::kFsrCasSpv[0] == 0x07230203, "SPIR-V magic must be 0x07230203");
    static_assert(hyperplay::kFsrCasSpvSizeBytes > 0, "SPIR-V size must be > 0");
    static_assert(hyperplay::kFsrCasSpvWordCount == hyperplay::kFsrCasSpvSizeBytes / 4, "Word count mismatch");
    std::printf("FsrCasSpv: magic=0x%08X size=%zu bytes (%zu words) OK\n",
                hyperplay::kFsrCasSpv[0], hyperplay::kFsrCasSpvSizeBytes, hyperplay::kFsrCasSpvWordCount);
    return 0;
}
