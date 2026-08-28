#include "Engine/Core/Application.h"
#include "Engine/Core/Engine.h"
#include "Game/PhysicalPlayground.h"

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

        const DeepRun::Core::ApplicationOptions options = DeepRun::Core::ApplicationOptions::Parse(arguments);
        DeepRun::Game::PhysicalPlayground playground;
        DeepRun::Core::Application application(
            options,
            [&options, &playground](DeepRun::Core::Engine& engine)
            {
                if (options.headless)
                {
                    return true;
                }

                DeepRun::Render::D3D12Renderer* renderer = engine.Renderer();
                if (renderer == nullptr)
                {
                    std::cerr << "[Game][ERROR] Physical playground requires a windowed renderer\n";
                    return false;
                }

                const auto initialized = playground.Initialize(engine.Assets(), *renderer, options.smokeTest);
                if (!initialized)
                {
                    std::cerr << "[Game][ERROR] " << initialized.error() << '\n';
                    return false;
                }
                return playground.SubmarineModel().IsValid();
            },
            [&playground](DeepRun::Render::D3D12Renderer& renderer)
            {
                const auto rendered = playground.Render(renderer);
                if (!rendered)
                {
                    std::cerr << "[Game][ERROR] " << rendered.error() << '\n';
                    return false;
                }
                return rendered->drawCalls == 4 && rendered->submittedPrimitives == 4 &&
                       rendered->submittedIndices == 1632;
            });
        return application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[Core][ERROR] Unhandled startup failure: " << exception.what() << '\n';
        return 1;
    }
}
