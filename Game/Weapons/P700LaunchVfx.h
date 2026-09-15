#pragma once

#include "Engine/Render/Camera.h"
#include "Engine/Render/TransientVfx.h"
#include "Simulation/Weapons/P700Granit.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DeepRun::Game::Armament
{
enum class P700VfxLod : std::uint32_t
{
    Lod0Hero = 0U,
    Lod1Medium,
    Lod2Far,
    Lod3Strategic,
};

enum class P700LaunchAudioEvent : std::uint32_t
{
    LauncherOpen,
    UnderwaterIgnition,
    UnderwaterPass,
    SurfaceBreach,
    BoosterAir,
    WingDeploy,
    BoosterSeparate,
    TurbojetStart,
    Flyby,
};

[[nodiscard]] const char* P700LaunchAudioEventName(P700LaunchAudioEvent event) noexcept;

struct P700LaunchAudioHook final
{
    P700LaunchAudioEvent event = P700LaunchAudioEvent::LauncherOpen;
    std::uint64_t launchSeed = 0U;
    std::array<float, 3> worldPositionMeters{};
    bool underwater = false;
};

struct P700LaunchCameraImpulse final
{
    std::array<float, 3> worldPositionMeters{};
    float amplitude = 0.0F;
    float frequencyHz = 0.0F;
    float durationSeconds = 0.0F;
};

// Read-only presentation snapshot. Mission/weather code can fill this directly from the existing WeatherState;
// it is not a second weather authority and never feeds back into missile simulation.
struct P700LaunchVfxEnvironment final
{
    float significantWaveHeightMeters = 0.0F;
    float windSpeedMetersPerSecond = 0.0F;
    float windDirectionRadians = 0.0F;
    float rainRateMillimetersPerHour = 0.0F;
    float humidityFraction = 0.55F;
    float daylightFraction = 1.0F;
    bool lightningVisible = false;
};

struct P700LaunchVfxTuning final
{
    std::array<float, 3> lodDistancesMeters{260.0F, 900.0F, 3'000.0F};
    std::array<float, 4> lodParticleScales{1.0F, 0.58F, 0.24F, 0.075F};

    std::uint32_t bubbleMicroCount = 320U;
    std::uint32_t bubbleMesoCount = 96U;
    std::uint32_t bubbleMacroCount = 28U;
    float bubbleLifetimeSeconds = 4.8F;
    std::array<float, 2> bubbleMicroSizeMeters{0.025F, 0.085F};
    std::array<float, 2> bubbleMesoSizeMeters{0.12F, 0.42F};
    std::array<float, 2> bubbleMacroSizeMeters{0.55F, 1.85F};
    float underwaterTrailRadiusMeters = 2.8F;
    float underwaterTurbulence = 5.5F;

    float underwaterCoreLengthMeters = 1.8F;
    float underwaterCoreRadiusMeters = 0.38F;
    float underwaterEmissiveIntensity = 7.5F;
    std::uint32_t underwaterCoreParticleCount = 56U;

    float breachPreReactionDepthMeters = 6.5F;
    float splashRadiusMeters = 8.5F;
    std::uint32_t waterCrownCount = 84U;
    float waterCrownLifetimeSeconds = 1.45F;
    std::uint32_t dropletCount = 220U;
    std::array<float, 2> dropletSizeMeters{0.035F, 0.18F};
    float dropletLifetimeSeconds = 2.2F;
    std::uint32_t mistCount = 110U;
    float mistLifetimeSeconds = 2.8F;
    float mistDensity = 0.62F;
    std::uint32_t waterSheetCount = 54U;
    float waterSheetLifetimeSeconds = 1.15F;

    std::uint32_t foamCount = 150U;
    float foamLifetimeSeconds = 8.0F;
    float foamRadiusMeters = 11.0F;

    std::uint32_t airborneCoreCount = 72U;
    std::uint32_t airbornePlumeCount = 150U;
    float airborneCoreLengthMeters = 3.2F;
    float airbornePlumeLengthMeters = 11.0F;
    float airborneEmissiveIntensity = 10.0F;
    float airbornePlumeLifetimeSeconds = 0.9F;

    std::uint32_t cruiseExhaustCount = 48U;
    float cruiseExhaustLengthMeters = 4.5F;
    float cruiseExhaustOpacity = 0.20F;
    float cruiseExhaustLifetimeSeconds = 0.55F;

    float condensationHumidityThreshold = 0.82F;
    std::uint32_t condensationMistCount = 42U;
    float condensationLifetimeSeconds = 0.45F;

    float cameraImpulseAmplitudeAtTenMeters = 0.085F;
    float cameraImpulseFrequencyHz = 7.0F;
    float cameraImpulseDurationSeconds = 0.42F;
    float cameraImpulseMaximumDistanceMeters = 180.0F;

    std::uint32_t maximumTrackedLaunches = 32U;
};

[[nodiscard]] P700LaunchVfxTuning DefaultP700LaunchVfxTuning() noexcept;
[[nodiscard]] std::expected<P700LaunchVfxTuning, std::string> LoadP700LaunchVfxTuning(
    const std::filesystem::path& path);
[[nodiscard]] std::expected<void, std::string> ValidateP700LaunchVfxTuning(
    const P700LaunchVfxTuning& tuning);

struct P700LaunchVfxFrame final
{
    std::vector<Render::TransientVfxEmitter> emitters{};
    std::vector<P700LaunchAudioHook> audioHooks{};
    std::optional<P700LaunchCameraImpulse> cameraImpulse{};
    std::array<std::uint32_t, 4> missileCountByLod{};
    std::uint32_t requestedParticles = 0U;
    std::uint32_t submittedParticles = 0U;
    bool particleBudgetClamped = false;
};

// Persistent presentation-only event history. It observes immutable P-700 gameplay states and remembers only
// what is needed for trails/foam/audio edge events after a missile has moved on. Rendering never becomes authority.
class P700LaunchVfxSystem final
{
public:
    explicit P700LaunchVfxSystem(P700LaunchVfxTuning tuning = DefaultP700LaunchVfxTuning());

    [[nodiscard]] std::expected<P700LaunchVfxFrame, std::string> BuildFrame(
        std::span<const Weapons::P700GranitRuntimeState* const> missiles,
        const Render::OrthographicCamera& camera,
        const P700LaunchVfxEnvironment& environment,
        double simulationTimeSeconds);

    [[nodiscard]] const P700LaunchVfxTuning& Tuning() const noexcept { return tuning_; }
    void SetTuning(P700LaunchVfxTuning tuning) noexcept { tuning_ = tuning; }
    void Reset() noexcept;

private:
    struct LaunchRecord final
    {
        std::uint64_t key = 0U;
        Weapons::P700GranitPhase previousPhase = Weapons::P700GranitPhase::Stored;
        bool previousMainEngineActive = false;
        bool previousBoosterAttached = true;
        bool initialized = false;
        bool hasUnderwaterOrigin = false;
        std::array<float, 3> underwaterOrigin{};
        bool hasBreach = false;
        std::array<float, 3> breachPosition{};
        std::array<float, 3> launchDirection{1.0F, 0.0F, 0.0F};
        double breachTimeSeconds = 0.0;
        double lastSeenTimeSeconds = 0.0;
        bool flybyHookEmitted = false;
    };

    [[nodiscard]] LaunchRecord& ResolveRecord(const Weapons::P700GranitRuntimeState& missile, double timeSeconds);
    [[nodiscard]] P700VfxLod SelectLod(
        const Weapons::P700GranitRuntimeState& missile,
        const Render::OrthographicCamera& camera) const noexcept;

    P700LaunchVfxTuning tuning_{};
    std::vector<LaunchRecord> records_{};
};
} // namespace DeepRun::Game::Armament
