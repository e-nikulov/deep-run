#include "Engine/Core/CoreServices.h"

namespace DeepRun::Core
{
CoreServices::CoreServices()
{
    logger_.Info(Diagnostics::LogCategory::Core, "DeepRun Engine starting");
}

CoreServices::~CoreServices()
{
    logger_.Info(Diagnostics::LogCategory::Core, "Core services shut down");
}

Diagnostics::Logger& CoreServices::Log() noexcept
{
    return logger_;
}
}
