from pathlib import Path

path = Path("Game/Combat/CombatPlaygroundRuntime.h")
text = path.read_text(encoding="utf-8")

old = '''            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, playerSnapshot.passiveReceiver, simulationTimeSeconds);
            if (!observed)
            {
                return std::unexpected("M5-J3 incoming-threat acoustic propagation failed: " +
                                       observed.error().message);
            }
'''
new = '''            const float referenceSurfaceYMeters =
                playerSnapshot.emitter.positionMeters.y + playerSnapshot.signedDepthMeters;
            const auto environment = Acoustics::EvaluateAcousticEnvironmentPath(
                emission->positionMeters,
                playerSnapshot.passiveReceiver.positionMeters,
                referenceSurfaceYMeters,
                0.0F);
            if (!environment)
            {
                return std::unexpected("M5-J3 incoming-threat environment path failed: " + environment.error());
            }
            const auto observed = acousticWorld_.CollectPassiveDirectObservation(
                *emission, playerSnapshot.passiveReceiver, simulationTimeSeconds, *environment);
            if (!observed)
            {
                return std::unexpected("M5-J3 incoming-threat acoustic propagation failed: " +
                                       observed.error().message);
            }
'''

if text.count(old) != 1:
    raise SystemExit("incoming threat acoustic path anchor mismatch")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8")
