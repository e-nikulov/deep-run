from pathlib import Path

path = Path("Game/Combat/CombatPlaygroundRuntime.h")
text = path.read_text(encoding="utf-8")

old = '''            destroyerTorpedoActiveReflector_ = Acoustics::AcousticReflector{
                .positionMeters = playerSnapshot.emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            destroyerTorpedoActivePulseDeadlineSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoActiveListenWindowSeconds;
'''
new = '''            // An active seeker is not acoustically invisible. Publish its outgoing transmission into the same
            // incoming-threat acoustic evidence queue used by machinery noise. Propagation delay, environmental
            // loss, SNR and TrackManager association remain authoritative; no torpedo body identity is exposed.
            const auto outgoingPing = Acoustics::MakeActiveTransmissionEmission(*destroyerTorpedoActivePulse_);
            if (!outgoingPing)
                return std::unexpected("M5 hostile torpedo active transmission emission failed: " + outgoingPing.error());
            pendingIncomingThreatEmissions_.push_back(*outgoingPing);
            destroyerTorpedoActiveReflector_ = Acoustics::AcousticReflector{
                .positionMeters = playerSnapshot.emitter.positionMeters,
                .reflectionLossDb = {.levelDb = {8.0F, 8.0F, 8.0F, 8.0F}}};
            destroyerTorpedoActivePulseDeadlineSeconds_ =
                simulationTimeSeconds + M5CombatTorpedoActiveListenWindowSeconds;
'''

if text.count(old) != 1:
    raise SystemExit("hostile torpedo active-ping anchor mismatch")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8")
