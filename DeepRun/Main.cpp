#include "Engine/Core/Application.h"

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int main(const int argumentCount, char** argumentValues)
{
    try
    {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argumentCount > 1 ? argumentCount - 1 : 0));
        for (int index = 1; index < argumentCount; ++index)
        {
            arguments.emplace_back(argumentValues[index]);
        }

        DeepRun::Core::Application application(DeepRun::Core::ApplicationOptions::Parse(arguments));
        return application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[Core][ERROR] Unhandled startup failure: " << exception.what() << '\n';
        return 1;
    }
}
