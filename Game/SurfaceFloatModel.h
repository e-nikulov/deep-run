#pragma once

#include "Engine/Assets/ModelAsset.h"

namespace DeepRun::Game
{
// M3-F's one Game-owned representative surface float. This is deliberately a fixed presentation helper,
// not a general primitive library or an asset pipeline.
[[nodiscard]] Assets::ModelAsset BuildM3SurfaceFloatModel();
}
