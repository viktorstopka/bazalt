#include "bazalt/engine/EngineVersion.h"

namespace bazalt::engine
{
    Version getVersion() noexcept
    {
        return { 0, 1, 0 };
    }

    const char* getVersionString() noexcept
    {
        return "0.1.0";
    }
}
