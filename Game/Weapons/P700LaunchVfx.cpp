#include "Game/Weapons/P700LaunchVfx.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace DeepRun::Game::Armament
{
namespace
{
using json = nlohmann::json;

[[nodiscard]] std::array<float, 3> ToArray(const Physics::PhysicsVector3& value) noexcept
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] std::array<float, 3> Normalize(const std::array<float, 3>& value) noexcept
{
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!std::isfinite(length) || length <= 1.0e-5F)
        return {1.0F, 0.0F, 0.0F};
    return {value[0] / length, value[1] / length, value[2] / length};
}

[[nodiscard]] float Distance(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    const float x = b[0] - a[0];
    const float y = b[1] - a[1];
    const float z = b[2] - a[2];
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] std::array<float, 3> Subtract(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

[[nodiscard]] std::array<float, 3> AddScaled(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& direction,
    const float scale) noexcept
{
    return {origin[0] + direction[0] * scale, origin[1] + direction[1] * scale, origin[2] + direction[2] * scale};
}

[[nodiscard]] std::uint64_t StableLaunchKey(const Weapons::P700GranitRuntimeState& missile) noexcept
{
    if (missile.terminalRandomSeed != 0U)
        return missile.terminalRandomSeed;
    std::uint64_t seed = missile.guidanceTrackId.value_or(0x50373030ULL);
    seed ^= static_cast<std::uint64_t>(std::llround((missile.positionMeters.x + 100'000.0F) * 10.0F)) << 1U;
    seed ^= static_cast<std::uint64_t>(std::llround((missile.positionMeters.z + 100'000.0F) * 10.0F)) << 17U;
    return Weapons::P700SplitMix64(seed);
}

[[nodiscard]] std::uint32_t ScaledCount(
    const std::uint32_t base,
    const float lodScale,
    const float salvoScale) noexcept
{
    return (std::max)(1U, static_cast<std::uint32_t>(std::lround(static_cast<double>(base) * lodScale * salvoScale)));
}

[[nodiscard]] bool FiniteEnvironment(const P700LaunchVfxEnvironment& environment) noexcept
{
    return std::isfinite(environment.significantWaveHeightMeters) && environment.significantWaveHeightMeters >= 0.0F &&
           std::isfinite(environment.windSpeedMetersPerSecond) && environment.windSpeedMetersPerSecond >= 0.0F &&
           std::isfinite(environment.windDirectionRadians) &&
           std::isfinite(environment.rainRateMillimetersPerHour) && environment.rainRateMillimetersPerHour >= 0.0F &&
           std::isfinite(environment.humidityFraction) && environment.humidityFraction >= 0.0F && environment.humidityFraction <= 1.0F &&
           std::isfinite(environment.daylightFraction) && environment.daylightFraction >= 0.0F && environment.daylightFraction <= 1.0F;
}

template <typename T>
void ReadIfPresent(const json& object, const char* name, T& value)
{
    if (const auto it = object.find(name); it != object.end())
        value = it->get<T>();
}

template <std::size_t N>
void ReadArrayIfPresent(const json& object, const char* name, std::array<float, N>& value)
{
    const auto it = object.find(name);
    if (it == object.end())
        return;
    if (!it->is_array() || it->size() != N)
        throw std::runtime_error(std::string(name) + " must contain exactly " + std::to_string(N) + " values");
    for (std::size_t index = 0; index < N; ++index)
        value[index] = (*it)[index].get<float>();
}
} // namespace

const char* P700LaunchAudioEventName(const P700LaunchAudioEvent event) noexcept
{
    switch (event)
    {
    case P700LaunchAudioEvent::LauncherOpen: return "P700_LAUNCHER_OPEN";
    case P700LaunchAudioEvent::UnderwaterIgnition: return "P700_UNDERWATER_IGNITION";
    case P700LaunchAudioEvent::UnderwaterPass: return "P700_UNDERWATER_PASS";
    case P700LaunchAudioEvent::SurfaceBreach: return "P700_SURFACE_BREACH";
    case P700LaunchAudioEvent::BoosterAir: return "P700_BOOSTER_AIR";
    case P700LaunchAudioEvent::WingDeploy: return "P700_WING_DEPLOY";
    case P700LaunchAudioEvent::BoosterSeparate: return "P700_BOOSTER_SEPARATE";
    case P700LaunchAudioEvent::TurbojetStart: return "P700_TURBOJET_START";
    case P700LaunchAudioEvent::Flyby: return "P700_FLYBY";
    }
    return "P700_UNKNOWN";
}

P700LaunchVfxTuning DefaultP700LaunchVfxTuning() noexcept
{
    return {};
}

std::expected<void, std::string> ValidateP700LaunchVfxTuning(const P700LaunchVfxTuning& t)
{
    const auto positive = [](const float value) { return std::isfinite(value) && value > 0.0F; };
    if (!positive(t.lodDistancesMeters[0]) || !positive(t.lodDistancesMeters[1]) || !positive(t.lodDistancesMeters[2]) ||
        !(t.lodDistancesMeters[0] < t.lodDistancesMeters[1] && t.lodDistancesMeters[1] < t.lodDistancesMeters[2]))
        return std::unexpected("P-700 VFX LOD distances must be finite, positive and strictly increasing");
    for (const float scale : t.lodParticleScales)
        if (!positive(scale) || scale > 1.0F)
            return std::unexpected("P-700 VFX LOD particle scales must be in (0,1]");
    if (t.bubbleMicroCount == 0U || t.bubbleMesoCount == 0U || t.bubbleMacroCount == 0U ||
        t.underwaterCoreParticleCount == 0U || t.waterCrownCount == 0U || t.dropletCount == 0U ||
        t.mistCount == 0U || t.waterSheetCount == 0U || t.foamCount == 0U || t.airborneCoreCount == 0U ||
        t.airbornePlumeCount == 0U || t.cruiseExhaustCount == 0U || t.maximumTrackedLaunches < 2U)
        return std::unexpected("P-700 VFX particle counts/tracked-launch capacity must be non-zero");
    const std::array<float, 22> scalars{
        t.bubbleLifetimeSeconds, t.underwaterTrailRadiusMeters, t.underwaterTurbulence,
        t.underwaterCoreLengthMeters, t.underwaterCoreRadiusMeters, t.underwaterEmissiveIntensity,
        t.breachPreReactionDepthMeters, t.splashRadiusMeters, t.waterCrownLifetimeSeconds,
        t.dropletLifetimeSeconds, t.mistLifetimeSeconds, t.mistDensity, t.waterSheetLifetimeSeconds,
        t.foamLifetimeSeconds, t.foamRadiusMeters, t.airborneCoreLengthMeters, t.airbornePlumeLengthMeters,
        t.airborneEmissiveIntensity, t.airbornePlumeLifetimeSeconds, t.cruiseExhaustLengthMeters,
        t.cruiseExhaustLifetimeSeconds, t.condensationLifetimeSeconds};
    if (std::ranges::any_of(scalars, [&](const float value) { return !positive(value); }))
        return std::unexpected("P-700 VFX positive tuning contains invalid scalar values");
    const auto validSizeRange = [&](const std::array<float, 2>& range) {
        return positive(range[0]) && positive(range[1]) && range[0] <= range[1];
    };
    if (!validSizeRange(t.bubbleMicroSizeMeters) || !validSizeRange(t.bubbleMesoSizeMeters) ||
        !validSizeRange(t.bubbleMacroSizeMeters) || !validSizeRange(t.dropletSizeMeters))
        return std::unexpected("P-700 VFX particle size ranges are invalid");
    if (!std::isfinite(t.cruiseExhaustOpacity) || t.cruiseExhaustOpacity < 0.0F || t.cruiseExhaustOpacity > 1.0F ||
        !std::isfinite(t.condensationHumidityThreshold) || t.condensationHumidityThreshold < 0.0F ||
        t.condensationHumidityThreshold > 1.0F || !positive(t.cameraImpulseMaximumDistanceMeters) ||
        !positive(t.cameraImpulseFrequencyHz) || !positive(t.cameraImpulseDurationSeconds) ||
        !std::isfinite(t.cameraImpulseAmplitudeAtTenMeters) || t.cameraImpulseAmplitudeAtTenMeters < 0.0F)
        return std::unexpected("P-700 VFX opacity/condensation/camera tuning is invalid");
    return {};
}

std::expected<P700LaunchVfxTuning, std::string> LoadP700LaunchVfxTuning(const std::filesystem::path& path)
{
    try
    {
        std::ifstream input(path);
        if (!input)
            return std::unexpected("unable to open P-700 VFX tuning: " + path.string());
        const json root = json::parse(input);
        P700LaunchVfxTuning t = DefaultP700LaunchVfxTuning();
        ReadArrayIfPresent(root, "lodDistancesMeters", t.lodDistancesMeters);
        ReadArrayIfPresent(root, "lodParticleScales", t.lodParticleScales);
        ReadIfPresent(root, "bubbleMicroCount", t.bubbleMicroCount);
        ReadIfPresent(root, "bubbleMesoCount", t.bubbleMesoCount);
        ReadIfPresent(root, "bubbleMacroCount", t.bubbleMacroCount);
        ReadIfPresent(root, "bubbleLifetimeSeconds", t.bubbleLifetimeSeconds);
        ReadArrayIfPresent(root, "bubbleMicroSizeMeters", t.bubbleMicroSizeMeters);
        ReadArrayIfPresent(root, "bubbleMesoSizeMeters", t.bubbleMesoSizeMeters);
        ReadArrayIfPresent(root, "bubbleMacroSizeMeters", t.bubbleMacroSizeMeters);
        ReadIfPresent(root, "underwaterTrailRadiusMeters", t.underwaterTrailRadiusMeters);
        ReadIfPresent(root, "underwaterTurbulence", t.underwaterTurbulence);
        ReadIfPresent(root, "underwaterCoreLengthMeters", t.underwaterCoreLengthMeters);
        ReadIfPresent(root, "underwaterCoreRadiusMeters", t.underwaterCoreRadiusMeters);
        ReadIfPresent(root, "underwaterEmissiveIntensity", t.underwaterEmissiveIntensity);
        ReadIfPresent(root, "underwaterCoreParticleCount", t.underwaterCoreParticleCount);
        ReadIfPresent(root, "breachPreReactionDepthMeters", t.breachPreReactionDepthMeters);
        ReadIfPresent(root, "splashRadiusMeters", t.splashRadiusMeters);
        ReadIfPresent(root, "waterCrownCount", t.waterCrownCount);
        ReadIfPresent(root, "waterCrownLifetimeSeconds", t.waterCrownLifetimeSeconds);
        ReadIfPresent(root, "dropletCount", t.dropletCount);
        ReadArrayIfPresent(root, "dropletSizeMeters", t.dropletSizeMeters);
        ReadIfPresent(root, "dropletLifetimeSeconds", t.dropletLifetimeSeconds);
        ReadIfPresent(root, "mistCount", t.mistCount);
        ReadIfPresent(root, "mistLifetimeSeconds", t.mistLifetimeSeconds);
        ReadIfPresent(root, "mistDensity", t.mistDensity);
        ReadIfPresent(root, "waterSheetCount", t.waterSheetCount);
        ReadIfPresent(root, "waterSheetLifetimeSeconds", t.waterSheetLifetimeSeconds);
        ReadIfPresent(root, "foamCount", t.foamCount);
        ReadIfPresent(root, "foamLifetimeSeconds", t.foamLifetimeSeconds);
        ReadIfPresent(root, "foamRadiusMeters", t.foamRadiusMeters);
        ReadIfPresent(root, "airborneCoreCount", t.airborneCoreCount);
        ReadIfPresent(root, "airbornePlumeCount", t.airbornePlumeCount);
        ReadIfPresent(root, "airborneCoreLengthMeters", t.airborneCoreLengthMeters);
        ReadIfPresent(root, "airbornePlumeLengthMeters", t.airbornePlumeLengthMeters);
        ReadIfPresent(root, "airborneEmissiveIntensity", t.airborneEmissiveIntensity);
        ReadIfPresent(root, "airbornePlumeLifetimeSeconds", t.airbornePlumeLifetimeSeconds);
        ReadIfPresent(root, "cruiseExhaustCount", t.cruiseExhaustCount);
        ReadIfPresent(root, "cruiseExhaustLengthMeters", t.cruiseExhaustLengthMeters);
        ReadIfPresent(root, "cruiseExhaustOpacity", t.cruiseExhaustOpacity);
        ReadIfPresent(root, "cruiseExhaustLifetimeSeconds", t.cruiseExhaustLifetimeSeconds);
        ReadIfPresent(root, "condensationHumidityThreshold", t.condensationHumidityThreshold);
        ReadIfPresent(root, "condensationMistCount", t.condensationMistCount);
        ReadIfPresent(root, "condensationLifetimeSeconds", t.condensationLifetimeSeconds);
        ReadIfPresent(root, "cameraImpulseAmplitudeAtTenMeters", t.cameraImpulseAmplitudeAtTenMeters);
        ReadIfPresent(root, "cameraImpulseFrequencyHz", t.cameraImpulseFrequencyHz);
        ReadIfPresent(root, "cameraImpulseDurationSeconds", t.cameraImpulseDurationSeconds);
        ReadIfPresent(root, "cameraImpulseMaximumDistanceMeters", t.cameraImpulseMaximumDistanceMeters);
        ReadIfPresent(root, "maximumTrackedLaunches", t.maximumTrackedLaunches);
        if (const auto valid = ValidateP700LaunchVfxTuning(t); !valid)
            return std::unexpected(valid.error());
        return t;
    }
    catch (const std::exception& exception)
    {
        return std::unexpected("P-700 VFX tuning parse failed: " + std::string(exception.what()));
    }
}

P700LaunchVfxSystem::P700LaunchVfxSystem(P700LaunchVfxTuning tuning) : tuning_(std::move(tuning))
{
    if (!ValidateP700LaunchVfxTuning(tuning_))
        tuning_ = DefaultP700LaunchVfxTuning();
}

void P700LaunchVfxSystem::Reset() noexcept
{
    records_.clear();
}

P700LaunchVfxSystem::LaunchRecord& P700LaunchVfxSystem::ResolveRecord(
    const Weapons::P700GranitRuntimeState& missile, const double timeSeconds)
{
    const std::uint64_t key = StableLaunchKey(missile);
    if (const auto it = std::ranges::find(records_, key, &LaunchRecord::key); it != records_.end())
    {
        it->lastSeenTimeSeconds = timeSeconds;
        return *it;
    }
    if (records_.size() >= tuning_.maximumTrackedLaunches)
    {
        const auto oldest = std::ranges::min_element(records_, {}, &LaunchRecord::lastSeenTimeSeconds);
        if (oldest != records_.end())
            records_.erase(oldest);
    }
    records_.push_back(LaunchRecord{.key = key, .lastSeenTimeSeconds = timeSeconds});
    return records_.back();
}

P700VfxLod P700LaunchVfxSystem::SelectLod(
    const Weapons::P700GranitRuntimeState& missile, const Render::OrthographicCamera& camera) const noexcept
{
    const std::array<float, 3> p = ToArray(missile.positionMeters);
    const std::array<float, 3> target{camera.target.x, camera.target.y, camera.target.z};
    const float distance = Distance(p, target);
    if (distance < tuning_.lodDistancesMeters[0]) return P700VfxLod::Lod0Hero;
    if (distance < tuning_.lodDistancesMeters[1]) return P700VfxLod::Lod1Medium;
    if (distance < tuning_.lodDistancesMeters[2]) return P700VfxLod::Lod2Far;
    return P700VfxLod::Lod3Strategic;
}

std::expected<P700LaunchVfxFrame, std::string> P700LaunchVfxSystem::BuildFrame(
    const std::span<const Weapons::P700GranitRuntimeState* const> missiles,
    const Render::OrthographicCamera& camera,
    const P700LaunchVfxEnvironment& environment,
    const double simulationTimeSeconds)
{
    if (!std::isfinite(simulationTimeSeconds) || simulationTimeSeconds < 0.0 || !Render::IsFinite(camera.viewProjection) ||
        !FiniteEnvironment(environment))
        return std::unexpected("P-700 launch VFX received invalid frame/environment input");
    if (const auto valid = ValidateP700LaunchVfxTuning(tuning_); !valid)
        return std::unexpected(valid.error());

    P700LaunchVfxFrame frame{};
    const std::size_t activeMissileCount = std::ranges::count_if(missiles, [](const auto* missile) {
        return missile != nullptr && missile->phase != Weapons::P700GranitPhase::Stored &&
               missile->phase != Weapons::P700GranitPhase::Spent;
    });
    const float salvoScale = activeMissileCount <= 2U ? 1.0F : (activeMissileCount <= 6U ? 0.72F : 0.34F);
    const float weatherSplashScale = std::clamp(
        1.0F + environment.significantWaveHeightMeters * 0.10F + environment.rainRateMillimetersPerHour * 0.006F,
        1.0F, 2.35F);

    const auto append = [&](Render::TransientVfxEmitter emitter)
    {
        frame.requestedParticles += emitter.particleCount;
        if (frame.emitters.size() >= Render::TransientVfxMaximumEmittersPerFrame ||
            frame.submittedParticles >= Render::TransientVfxMaximumParticlesPerFrame)
        {
            frame.particleBudgetClamped = true;
            return;
        }
        const std::uint32_t available = Render::TransientVfxMaximumParticlesPerFrame - frame.submittedParticles;
        if (emitter.particleCount > available)
        {
            emitter.particleCount = available;
            frame.particleBudgetClamped = true;
        }
        if (emitter.particleCount < 1U)
            return;
        frame.submittedParticles += emitter.particleCount;
        frame.emitters.push_back(emitter);
    };

    const auto addAudio = [&](const P700LaunchAudioEvent event, const LaunchRecord& record,
                              const Weapons::P700GranitRuntimeState& missile, const bool underwater)
    {
        frame.audioHooks.push_back(P700LaunchAudioHook{
            .event = event,
            .launchSeed = record.key,
            .worldPositionMeters = ToArray(missile.positionMeters),
            .underwater = underwater});
    };

    for (const Weapons::P700GranitRuntimeState* missilePointer : missiles)
    {
        if (missilePointer == nullptr)
            continue;
        const auto& missile = *missilePointer;
        if (!missile.positionMeters.IsFinite() || !missile.launchForwardUnitVector.IsFinite() ||
            !std::isfinite(missile.speedMetersPerSecond) || !std::isfinite(missile.surfaceLevelYMeters) ||
            !std::isfinite(missile.phaseStartTimeSeconds) || simulationTimeSeconds < missile.phaseStartTimeSeconds)
            return std::unexpected("P-700 launch VFX observed invalid missile presentation state");
        if (missile.phase == Weapons::P700GranitPhase::Stored || missile.phase == Weapons::P700GranitPhase::Spent)
            continue;

        LaunchRecord& record = ResolveRecord(missile, simulationTimeSeconds);
        const bool phaseChanged = !record.initialized || record.previousPhase != missile.phase;
        const std::array<float, 3> position = ToArray(missile.positionMeters);
        const std::array<float, 3> launchDirection = Normalize(ToArray(missile.launchForwardUnitVector));
        const std::array<float, 3> flightDirection = Normalize({
            std::cos(missile.headingRadians), std::sin(missile.headingRadians), 0.0F});
        const std::array<float, 3>& direction =
            missile.phase <= Weapons::P700GranitPhase::WaterExit ? launchDirection : flightDirection;
        record.launchDirection = launchDirection;

        if (phaseChanged)
        {
            switch (missile.phase)
            {
            case Weapons::P700GranitPhase::HatchOpening:
                addAudio(P700LaunchAudioEvent::LauncherOpen, record, missile, true);
                break;
            case Weapons::P700GranitPhase::UnderwaterLaunch:
                addAudio(P700LaunchAudioEvent::UnderwaterIgnition, record, missile, true);
                addAudio(P700LaunchAudioEvent::UnderwaterPass, record, missile, true);
                break;
            case Weapons::P700GranitPhase::WaterExit:
                addAudio(P700LaunchAudioEvent::SurfaceBreach, record, missile, false);
                addAudio(P700LaunchAudioEvent::BoosterAir, record, missile, false);
                break;
            case Weapons::P700GranitPhase::PostExitTransition:
                addAudio(P700LaunchAudioEvent::BoosterSeparate, record, missile, false);
                break;
            case Weapons::P700GranitPhase::AirborneDeploying:
                addAudio(P700LaunchAudioEvent::WingDeploy, record, missile, false);
                break;
            case Weapons::P700GranitPhase::Cruise:
                if (!record.flybyHookEmitted)
                {
                    addAudio(P700LaunchAudioEvent::Flyby, record, missile, false);
                    record.flybyHookEmitted = true;
                }
                break;
            default:
                break;
            }
        }
        if (missile.mainEngineActive && (!record.initialized || !record.previousMainEngineActive))
            addAudio(P700LaunchAudioEvent::TurbojetStart, record, missile, false);

        if (missile.phase == Weapons::P700GranitPhase::UnderwaterLaunch && !record.hasUnderwaterOrigin)
        {
            record.hasUnderwaterOrigin = true;
            record.underwaterOrigin = position;
        }
        if (missile.phase == Weapons::P700GranitPhase::WaterExit && !record.hasBreach)
        {
            record.hasBreach = true;
            record.breachPosition = {position[0], missile.surfaceLevelYMeters, position[2]};
            record.breachTimeSeconds = simulationTimeSeconds;
        }

        const auto lod = SelectLod(missile, camera);
        const std::size_t lodIndex = static_cast<std::size_t>(lod);
        ++frame.missileCountByLod[lodIndex];
        const float lodScale = tuning_.lodParticleScales[lodIndex];
        const float phaseAge = static_cast<float>(simulationTimeSeconds - missile.phaseStartTimeSeconds);
        const std::uint32_t seed = static_cast<std::uint32_t>(record.key ^ (record.key >> 32U));
        const std::array<float, 3> tail = AddScaled(position, direction, -4.75F);

        const auto emitterBase = [&](const Render::TransientVfxPrimitive primitive,
                                     const Render::TransientVfxBlendMode blend,
                                     const std::uint32_t count,
                                     const float age,
                                     const float lifetime) {
            Render::TransientVfxEmitter emitter{};
            emitter.primitive = primitive;
            emitter.blendMode = blend;
            emitter.seed = seed ^ (static_cast<std::uint32_t>(primitive) * 0x9e3779b9U);
            emitter.particleCount = count;
            emitter.originWorldMeters = position;
            emitter.directionWorldUnit = direction;
            emitter.ageSeconds = age;
            emitter.lifetimeSeconds = lifetime;
            emitter.windSpeedMetersPerSecond = environment.windSpeedMetersPerSecond;
            emitter.windDirectionRadians = environment.windDirectionRadians;
            return emitter;
        };

        if (missile.phase == Weapons::P700GranitPhase::HatchOpening && missile.launcherFloodProgress > 0.0F)
        {
            auto prepare = emitterBase(Render::TransientVfxPrimitive::BubbleMicro, Render::TransientVfxBlendMode::Alpha,
                                       ScaledCount(28U, lodScale, salvoScale), std::fmod(phaseAge, 1.4F), 1.5F);
            prepare.originWorldMeters = tail;
            prepare.extentMeters = {2.2F, 0.7F, 0.5F};
            prepare.minimumSizeMeters = 0.025F;
            prepare.maximumSizeMeters = 0.09F;
            prepare.opacity = 0.35F * missile.launcherFloodProgress;
            prepare.turbulence = 1.6F;
            prepare.applyDepthAttenuation = true;
            prepare.linearColor = {0.58F, 0.78F, 0.90F};
            append(prepare);
        }

        const bool underwaterActive = missile.phase == Weapons::P700GranitPhase::UnderwaterLaunch;
        if (underwaterActive || (record.hasBreach && simulationTimeSeconds - record.breachTimeSeconds < tuning_.bubbleLifetimeSeconds))
        {
            const float trailAge = underwaterActive
                ? (std::min)(phaseAge, tuning_.bubbleLifetimeSeconds * 0.92F)
                : static_cast<float>(simulationTimeSeconds - record.breachTimeSeconds);
            const std::array<float, 3> trailEnd = underwaterActive ? position : record.breachPosition;
            const std::array<float, 3> trailStart = record.hasUnderwaterOrigin ? record.underwaterOrigin : tail;
            const float trailLength = (std::max)(4.0F, Distance(trailStart, trailEnd));
            const std::array<float, 3> trailDirection = Normalize(Subtract(trailEnd, trailStart));

            const auto addBubbleScale = [&](const Render::TransientVfxPrimitive primitive, const std::uint32_t baseCount,
                                            const std::array<float, 2> sizes, const float opacity, const std::uint32_t salt) {
                auto bubbles = emitterBase(primitive, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(baseCount, lodScale, salvoScale), trailAge, tuning_.bubbleLifetimeSeconds);
                bubbles.seed ^= salt;
                bubbles.originWorldMeters = trailEnd;
                bubbles.directionWorldUnit = trailDirection;
                bubbles.extentMeters = {trailLength, tuning_.underwaterTrailRadiusMeters,
                                        tuning_.underwaterTrailRadiusMeters * 0.65F};
                bubbles.minimumSizeMeters = sizes[0];
                bubbles.maximumSizeMeters = sizes[1];
                bubbles.opacity = opacity;
                bubbles.turbulence = tuning_.underwaterTurbulence;
                bubbles.spawnRadiusMeters = tuning_.underwaterTrailRadiusMeters * 0.25F;
                bubbles.applyDepthAttenuation = true;
                bubbles.linearColor = {0.70F, 0.86F, 0.94F};
                append(bubbles);
            };
            addBubbleScale(Render::TransientVfxPrimitive::BubbleMicro, tuning_.bubbleMicroCount,
                           tuning_.bubbleMicroSizeMeters, 0.52F, 0x101U);
            addBubbleScale(Render::TransientVfxPrimitive::BubbleMeso, tuning_.bubbleMesoCount,
                           tuning_.bubbleMesoSizeMeters, 0.62F, 0x202U);
            addBubbleScale(Render::TransientVfxPrimitive::BubbleMacro, tuning_.bubbleMacroCount,
                           tuning_.bubbleMacroSizeMeters, 0.48F, 0x303U);
        }

        if (underwaterActive && missile.launchBoosterActive)
        {
            auto core = emitterBase(Render::TransientVfxPrimitive::ExhaustCore, Render::TransientVfxBlendMode::Additive,
                ScaledCount(tuning_.underwaterCoreParticleCount, lodScale, salvoScale), std::fmod(phaseAge, 0.30F), 0.34F);
            core.originWorldMeters = tail;
            core.extentMeters = {tuning_.underwaterCoreLengthMeters, tuning_.underwaterCoreRadiusMeters,
                                 tuning_.underwaterCoreRadiusMeters};
            core.minimumSizeMeters = 0.09F;
            core.maximumSizeMeters = 0.34F;
            core.opacity = 0.78F;
            core.emissiveIntensity = tuning_.underwaterEmissiveIntensity;
            core.turbulence = 4.0F;
            core.applyDepthAttenuation = true;
            core.linearColor = {2.6F, 1.55F, 0.72F};
            append(core);

            auto particulate = emitterBase(Render::TransientVfxPrimitive::Particulate, Render::TransientVfxBlendMode::Alpha,
                ScaledCount(74U, lodScale, salvoScale), std::fmod(phaseAge, 1.5F), 1.6F);
            particulate.originWorldMeters = position;
            particulate.extentMeters = {12.0F, 2.6F, 1.4F};
            particulate.minimumSizeMeters = 0.018F;
            particulate.maximumSizeMeters = 0.075F;
            particulate.opacity = 0.26F;
            particulate.turbulence = 6.0F;
            particulate.applyDepthAttenuation = true;
            particulate.linearColor = {0.34F, 0.48F, 0.52F};
            append(particulate);

            const float depth = missile.surfaceLevelYMeters - missile.positionMeters.y;
            if (depth >= 0.0F && depth < tuning_.breachPreReactionDepthMeters)
            {
                const float reaction = 1.0F - depth / tuning_.breachPreReactionDepthMeters;
                auto aeration = emitterBase(Render::TransientVfxPrimitive::BubbleMacro, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(58U, lodScale, salvoScale), std::fmod(phaseAge, 0.8F), 0.9F);
                aeration.originWorldMeters = {position[0], missile.surfaceLevelYMeters - 0.18F, position[2]};
                aeration.extentMeters = {2.5F + 4.5F * reaction, 1.8F + 3.2F * reaction, 1.2F};
                aeration.minimumSizeMeters = 0.18F;
                aeration.maximumSizeMeters = 0.95F;
                aeration.opacity = 0.42F * reaction;
                aeration.turbulence = 7.0F;
                aeration.applyDepthAttenuation = true;
                aeration.linearColor = {0.72F, 0.88F, 0.96F};
                append(aeration);
            }

            const std::array<float, 3> target{camera.target.x, camera.target.y, camera.target.z};
            const float cameraDistance = Distance(position, target);
            if (phaseChanged && cameraDistance <= tuning_.cameraImpulseMaximumDistanceMeters)
            {
                const float amplitude = tuning_.cameraImpulseAmplitudeAtTenMeters * 10.0F /
                    (std::max)(10.0F, cameraDistance);
                if (!frame.cameraImpulse || amplitude > frame.cameraImpulse->amplitude)
                    frame.cameraImpulse = P700LaunchCameraImpulse{
                        .worldPositionMeters = position,
                        .amplitude = amplitude,
                        .frequencyHz = tuning_.cameraImpulseFrequencyHz,
                        .durationSeconds = tuning_.cameraImpulseDurationSeconds};
            }
        }

        if (record.hasBreach)
        {
            const float breachAge = static_cast<float>(simulationTimeSeconds - record.breachTimeSeconds);
            if (breachAge < tuning_.waterCrownLifetimeSeconds)
            {
                auto crown = emitterBase(Render::TransientVfxPrimitive::RadialCrown, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(tuning_.waterCrownCount, lodScale, salvoScale), breachAge,
                    tuning_.waterCrownLifetimeSeconds);
                crown.originWorldMeters = record.breachPosition;
                crown.directionWorldUnit = record.launchDirection;
                crown.extentMeters = {tuning_.splashRadiusMeters * 0.78F * weatherSplashScale,
                                     5.4F * weatherSplashScale,
                                     tuning_.splashRadiusMeters * 0.52F * weatherSplashScale};
                crown.minimumSizeMeters = 0.16F;
                crown.maximumSizeMeters = 0.72F;
                crown.opacity = 0.72F;
                crown.turbulence = 4.0F;
                crown.linearColor = {0.55F, 0.72F, 0.82F};
                append(crown);
            }
            if (breachAge < tuning_.dropletLifetimeSeconds)
            {
                auto droplets = emitterBase(Render::TransientVfxPrimitive::Droplet, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(tuning_.dropletCount, lodScale, salvoScale), breachAge, tuning_.dropletLifetimeSeconds);
                droplets.originWorldMeters = record.breachPosition;
                droplets.directionWorldUnit = record.launchDirection;
                droplets.baseVelocityMetersPerSecond = {
                    record.launchDirection[0] * 8.0F, 7.5F + record.launchDirection[1] * 7.0F,
                    record.launchDirection[2] * 8.0F};
                droplets.extentMeters = {tuning_.splashRadiusMeters * 0.72F * weatherSplashScale,
                                         9.5F * weatherSplashScale, 4.2F * weatherSplashScale};
                droplets.minimumSizeMeters = tuning_.dropletSizeMeters[0];
                droplets.maximumSizeMeters = tuning_.dropletSizeMeters[1];
                droplets.opacity = 0.82F;
                droplets.linearColor = {0.70F, 0.82F, 0.88F};
                append(droplets);
            }
            if (breachAge < tuning_.waterSheetLifetimeSeconds)
            {
                auto sheets = emitterBase(Render::TransientVfxPrimitive::RibbonSheet, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(tuning_.waterSheetCount, lodScale, salvoScale), breachAge, tuning_.waterSheetLifetimeSeconds);
                sheets.originWorldMeters = AddScaled(record.breachPosition, record.launchDirection, 2.0F);
                sheets.directionWorldUnit = record.launchDirection;
                sheets.baseVelocityMetersPerSecond = {
                    record.launchDirection[0] * 12.0F, 4.0F + record.launchDirection[1] * 10.0F,
                    record.launchDirection[2] * 12.0F};
                sheets.extentMeters = {5.0F * weatherSplashScale, 3.5F * weatherSplashScale, 2.8F};
                sheets.minimumSizeMeters = 0.18F;
                sheets.maximumSizeMeters = 0.72F;
                sheets.opacity = 0.58F;
                sheets.linearColor = {0.45F, 0.66F, 0.78F};
                append(sheets);
            }
            if (breachAge < tuning_.mistLifetimeSeconds)
            {
                auto mist = emitterBase(Render::TransientVfxPrimitive::Mist, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(tuning_.mistCount, lodScale, salvoScale), breachAge, tuning_.mistLifetimeSeconds);
                mist.originWorldMeters = record.breachPosition;
                mist.directionWorldUnit = record.launchDirection;
                mist.baseVelocityMetersPerSecond = {0.0F, 2.5F, 0.0F};
                mist.extentMeters = {tuning_.splashRadiusMeters * weatherSplashScale,
                                     5.5F * weatherSplashScale, 3.6F * weatherSplashScale};
                mist.minimumSizeMeters = 0.16F;
                mist.maximumSizeMeters = 0.78F;
                mist.opacity = std::clamp(tuning_.mistDensity * weatherSplashScale, 0.0F, 0.78F);
                mist.linearColor = {0.80F, 0.86F, 0.90F};
                append(mist);
            }
            if (breachAge < tuning_.foamLifetimeSeconds)
            {
                auto foam = emitterBase(Render::TransientVfxPrimitive::Foam, Render::TransientVfxBlendMode::Alpha,
                    ScaledCount(tuning_.foamCount, lodScale, salvoScale), breachAge, tuning_.foamLifetimeSeconds);
                foam.originWorldMeters = record.breachPosition;
                foam.directionWorldUnit = {1.0F, 0.0F, 0.0F};
                foam.extentMeters = {tuning_.foamRadiusMeters * (1.0F + 0.12F * weatherSplashScale), 0.08F,
                                     tuning_.foamRadiusMeters * 0.58F * (1.0F + 0.12F * weatherSplashScale)};
                foam.minimumSizeMeters = 0.18F;
                foam.maximumSizeMeters = 0.75F;
                foam.opacity = 0.66F;
                foam.linearColor = {0.74F, 0.82F, 0.84F};
                append(foam);
            }
        }

        const bool airborneBooster = missile.launchBoosterActive && missile.positionMeters.y >= missile.surfaceLevelYMeters - 0.15F;
        if (airborneBooster)
        {
            auto airCore = emitterBase(Render::TransientVfxPrimitive::ExhaustCore, Render::TransientVfxBlendMode::Additive,
                ScaledCount(tuning_.airborneCoreCount, lodScale, salvoScale),
                std::fmod(phaseAge, tuning_.airbornePlumeLifetimeSeconds * 0.85F), tuning_.airbornePlumeLifetimeSeconds);
            airCore.originWorldMeters = tail;
            airCore.extentMeters = {tuning_.airborneCoreLengthMeters, 0.62F, 0.48F};
            airCore.minimumSizeMeters = 0.10F;
            airCore.maximumSizeMeters = 0.38F;
            airCore.opacity = 0.92F;
            airCore.emissiveIntensity = tuning_.airborneEmissiveIntensity;
            airCore.turbulence = 5.0F;
            airCore.linearColor = {3.4F, 1.52F, 0.42F};
            append(airCore);

            auto plume = emitterBase(Render::TransientVfxPrimitive::ExhaustTurbulent, Render::TransientVfxBlendMode::Alpha,
                ScaledCount(tuning_.airbornePlumeCount, lodScale, salvoScale),
                std::fmod(phaseAge, tuning_.airbornePlumeLifetimeSeconds * 0.9F), tuning_.airbornePlumeLifetimeSeconds);
            plume.originWorldMeters = tail;
            plume.extentMeters = {tuning_.airbornePlumeLengthMeters, 2.2F, 1.2F};
            plume.minimumSizeMeters = 0.16F;
            plume.maximumSizeMeters = 0.82F;
            plume.opacity = 0.46F;
            plume.emissiveIntensity = 1.15F;
            plume.turbulence = 8.0F;
            plume.linearColor = {1.08F, 0.58F, 0.30F};
            append(plume);
        }
        else if (missile.mainEngineActive &&
                 (missile.phase == Weapons::P700GranitPhase::PostExitTransition ||
                  missile.phase == Weapons::P700GranitPhase::AirborneDeploying ||
                  missile.phase == Weapons::P700GranitPhase::Cruise ||
                  missile.phase == Weapons::P700GranitPhase::Terminal))
        {
            auto cruise = emitterBase(Render::TransientVfxPrimitive::ExhaustTurbulent, Render::TransientVfxBlendMode::Alpha,
                ScaledCount(tuning_.cruiseExhaustCount, lodScale, salvoScale),
                std::fmod(phaseAge, tuning_.cruiseExhaustLifetimeSeconds * 0.9F), tuning_.cruiseExhaustLifetimeSeconds);
            cruise.originWorldMeters = tail;
            cruise.extentMeters = {tuning_.cruiseExhaustLengthMeters, 0.7F, 0.45F};
            cruise.minimumSizeMeters = 0.08F;
            cruise.maximumSizeMeters = 0.30F;
            cruise.opacity = tuning_.cruiseExhaustOpacity;
            cruise.emissiveIntensity = 0.10F;
            cruise.turbulence = 5.0F;
            cruise.linearColor = {0.62F, 0.58F, 0.54F};
            append(cruise);
        }

        if (missile.phase == Weapons::P700GranitPhase::AirborneDeploying &&
            environment.humidityFraction >= tuning_.condensationHumidityThreshold &&
            phaseAge < tuning_.condensationLifetimeSeconds)
        {
            auto condensation = emitterBase(Render::TransientVfxPrimitive::Mist, Render::TransientVfxBlendMode::Alpha,
                ScaledCount(tuning_.condensationMistCount, lodScale, salvoScale), phaseAge,
                tuning_.condensationLifetimeSeconds);
            condensation.extentMeters = {4.0F, 1.1F, 2.4F};
            condensation.minimumSizeMeters = 0.06F;
            condensation.maximumSizeMeters = 0.26F;
            condensation.opacity = 0.28F;
            condensation.linearColor = {0.88F, 0.91F, 0.92F};
            append(condensation);
        }

        record.previousPhase = missile.phase;
        record.previousMainEngineActive = missile.mainEngineActive;
        record.previousBoosterAttached = missile.launchBoosterAttached;
        record.initialized = true;
    }

    const double staleAge = (std::max)(static_cast<double>(tuning_.foamLifetimeSeconds),
                                      static_cast<double>(tuning_.bubbleLifetimeSeconds)) + 2.0;
    std::erase_if(records_, [&](const LaunchRecord& record) {
        return simulationTimeSeconds - record.lastSeenTimeSeconds > staleAge;
    });

    if (const auto valid = Render::ValidateTransientVfxBatch(frame.emitters); !valid)
        return std::unexpected("P-700 launch VFX produced an invalid GPU batch: " + valid.error());
    return frame;
}
} // namespace DeepRun::Game::Armament
