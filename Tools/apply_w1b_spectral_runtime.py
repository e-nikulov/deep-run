from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def patch_renderer() -> None:
    path = ROOT / "Engine" / "Render" / "D3D12Renderer.cpp"
    replace_once(
        path,
        """struct GerstnerDrawConstants final
{
    std::array<float, 16> viewProjection{};
    std::array<float, 4> referenceLevelAndTime{};
    std::array<float, 4> wave0{};
    std::array<float, 4> wave1{};
    std::array<float, 4> wave2{};
    std::array<float, 4> horizontalSteepness{};
    std::array<float, 4> deepFillColor{};
    std::array<float, 4> surfaceTintColor{};
};

static_assert(sizeof(GerstnerDrawConstants) == sizeof(std::uint32_t) * 44U);
constexpr UINT GerstnerRootSignatureDwordCost = sizeof(GerstnerDrawConstants) / sizeof(std::uint32_t);
static_assert(GerstnerRootSignatureDwordCost == 44U);
static_assert(GerstnerRootSignatureDwordCost < D3D12_MAX_ROOT_COST);
""",
        """struct GerstnerDrawConstants final
{
    std::array<float, 16> viewProjection{};
    std::array<float, 4> referenceLevelAndTime{};
    std::array<float, 4> wave0{};
    std::array<float, 4> wave1{};
    std::array<float, 4> wave2{};
    std::array<float, 4> wave3{};
    std::array<float, 4> wave4{};
    std::array<float, 4> wave5{};
    std::array<float, 4> wave6{};
    std::array<float, 4> horizontalSteepness0{};
    std::array<float, 4> horizontalSteepness1{};
    std::array<float, 4> deepFillColor{};
    std::array<float, 4> surfaceTintColor{};
};

static_assert(sizeof(GerstnerDrawConstants) == sizeof(std::uint32_t) * 64U);
constexpr UINT GerstnerRootSignatureDwordCost = sizeof(GerstnerDrawConstants) / sizeof(std::uint32_t);
static_assert(GerstnerRootSignatureDwordCost == 64U);
static_assert(GerstnerRootSignatureDwordCost <= D3D12_MAX_ROOT_COST);
""",
    )
    replace_once(
        path,
        """            .wave0 = asConstants(parameters.components[0]),
            .wave1 = asConstants(parameters.components[1]),
            .wave2 = asConstants(parameters.components[2]),
            .horizontalSteepness = {
                parameters.components[0].horizontalSteepness,
                parameters.components[1].horizontalSteepness,
                parameters.components[2].horizontalSteepness,
                0.0F},
""",
        """            .wave0 = asConstants(parameters.components[0]),
            .wave1 = asConstants(parameters.components[1]),
            .wave2 = asConstants(parameters.components[2]),
            .wave3 = asConstants(parameters.components[3]),
            .wave4 = asConstants(parameters.components[4]),
            .wave5 = asConstants(parameters.components[5]),
            .wave6 = asConstants(parameters.components[6]),
            .horizontalSteepness0 = {
                parameters.components[0].horizontalSteepness,
                parameters.components[1].horizontalSteepness,
                parameters.components[2].horizontalSteepness,
                parameters.components[3].horizontalSteepness},
            .horizontalSteepness1 = {
                parameters.components[4].horizontalSteepness,
                parameters.components[5].horizontalSteepness,
                parameters.components[6].horizontalSteepness,
                0.0F},
""",
    )
    replace_once(
        path,
        '"M3-E Gerstner surface configured: vertices="',
        '"W1-B spectral surface configured: vertices="',
    )


def patch_playground() -> None:
    path = ROOT / "Game" / "PhysicalPlayground.cpp"
    replace_once(
        path,
        '#include "Game/WaterPresentation.h"\n#include "Simulation/Marine/BuoyancySystem.h"\n',
        '#include "Game/WaterPresentation.h"\n#include "Simulation/Environment/WeatherSeaState.h"\n#include "Simulation/Marine/BuoyancySystem.h"\n#include "Simulation/Marine/ProductionOceanSpectrum.h"\n',
    )
    replace_once(
        path,
        """    // D2/E3 scenario composition: this playground owns its authoritative water body as a plain value, created
    // through the D1 validated factory with Game-owned M2 tuning. No MarineEnvironment/global/singleton —
    // Engine/Core stays unaware of water (architecture scan). The WaterBody knows nothing about the renderer,
    // camera or submarine; it only answers surface/depth queries.
    const auto water = Marine::WaterBody::Create(
        {.surfaceLevelY = M2SeaSurfaceLevelMeters,
         .densityKgPerCubicMeter = M2SeaWaterDensityKgPerCubicMeter,
         .waves = Marine::M3WaterWaveField});
""",
        """    // W1-B scenario composition: WeatherState is the environment input authority and Marine derives one
    // deterministic seven-component spectrum from it. The normal gameplay seed intentionally combines a
    // moderate local wind sea with a longer oblique swell so the surface has natural beats instead of a
    // repeating three-sine silhouette. Render still receives only a copied WaterBody snapshot.
    const auto weather = Environment::WeatherState::Create(Environment::WeatherStateConfig{
        .beaufortForce = 5U,
        .windSpeedMetersPerSecond = 10.0F,
        .windGustSpeedMetersPerSecond = 13.0F,
        .windDirectionDegrees = 18.0F,
        .windSea = Environment::WindSeaState{
            .significantWaveHeightMeters = 2.0F,
            .probableMaximumWaveHeightMeters = 2.5F,
            .peakPeriodSeconds = 4.8F,
            .meanDirectionDegrees = 18.0F,
            .directionalSpreadDegrees = 42.0F},
        .swell = Environment::SwellState{
            .significantWaveHeightMeters = 1.2F,
            .peakPeriodSeconds = 9.5F,
            .meanDirectionDegrees = 342.0F,
            .directionalSpreadDegrees = 10.0F},
        .rainRateMillimetersPerHour = 0.0F,
        .meteorologicalVisibilityMeters = 100000.0F,
        .cloudCoverFraction = 0.25F,
        .lightningRatePerMinute = 0.0F,
        .weatherSeed = 0x4452554E5F573142ULL});
    if (!weather)
    {
        return std::unexpected("physical playground W1 weather creation failed: " + weather.error().message);
    }
    const auto productionWaves = Marine::BuildProductionOceanWaveField(*weather);
    if (!productionWaves || !productionWaves->has_value())
    {
        return std::unexpected("physical playground W1 spectral ocean creation failed" +
                               (productionWaves ? std::string{} : ": " + productionWaves.error()));
    }
    const auto water = Marine::WaterBody::Create(
        {.surfaceLevelY = M2SeaSurfaceLevelMeters,
         .densityKgPerCubicMeter = M2SeaWaterDensityKgPerCubicMeter,
         .waves = productionWaves->value()});
""",
    )


def main() -> None:
    patch_renderer()
    patch_playground()
    print("W1-B spectral runtime patch: PASS")


if __name__ == "__main__":
    main()
