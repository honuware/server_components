#include "util/env.h"

#include <cstdlib>

namespace Util {

const char* GetEnvWithFallback(const char* primary, const char* fallback) {
    const char* value = std::getenv(primary);
    if (value != nullptr) {
        return value;
    }
    return std::getenv(fallback);
}

}  // namespace Util
