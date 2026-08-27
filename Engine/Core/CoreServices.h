#pragma once

#include "Engine/Diagnostics/Logger.h"

namespace DeepRun::Core
{
class CoreServices final
{
public:
    CoreServices();
    ~CoreServices();

    CoreServices(const CoreServices&) = delete;
    CoreServices& operator=(const CoreServices&) = delete;

    Diagnostics::Logger& Log() noexcept;

private:
    Diagnostics::Logger logger_;
};
}
