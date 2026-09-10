from pathlib import Path

p = Path('Game/Combat/CombatPlaygroundRuntime.h')
text = p.read_text(encoding='utf-8')
old = '''    void ApplyIncomingThreatPresentation(PlayerCombatPresentationSnapshot& snapshot) const noexcept
    {
        const Perception::Track* best = nullptr;
        for (const auto& track : incomingThreatTracks_.Tracks())
        {
            const bool present = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||
                                 track.lifecycle == Perception::TrackLifecycleState::Coasting;
            if (!present)
            {
                continue;
            }
            if (best == nullptr || track.confidence > best->confidence ||
                (track.confidence == best->confidence && track.trackId < best->trackId))
            {
                best = &track;
            }
        }
        if (best == nullptr)
        {
            return;
        }

        snapshot.incomingThreatDetected = true;
        snapshot.incomingThreatLifecycle = best->lifecycle;
        snapshot.incomingThreatBearingRadians = best->estimatedBearingRadians;
        snapshot.incomingThreatBearingUncertaintyRadians = best->bearingUncertaintyRadians;
        snapshot.incomingThreatConfidence = best->confidence;
    }
'''
new = '''    void ApplyIncomingThreatPresentation(PlayerCombatPresentationSnapshot& snapshot) const noexcept
    {
        // TrackManager::Tracks() intentionally returns a snapshot by value. Keep the selected perceived Track by
        // value as well; never retain a pointer/reference into that temporary snapshot beyond the loop.
        std::optional<Perception::Track> best{};
        for (const auto& track : incomingThreatTracks_.Tracks())
        {
            const bool present = track.lifecycle == Perception::TrackLifecycleState::Confirmed ||
                                 track.lifecycle == Perception::TrackLifecycleState::Coasting;
            if (!present)
            {
                continue;
            }
            if (!best || track.confidence > best->confidence ||
                (track.confidence == best->confidence && track.trackId < best->trackId))
            {
                best = track;
            }
        }
        if (!best)
        {
            return;
        }

        snapshot.incomingThreatDetected = true;
        snapshot.incomingThreatLifecycle = best->lifecycle;
        snapshot.incomingThreatBearingRadians = best->estimatedBearingRadians;
        snapshot.incomingThreatBearingUncertaintyRadians = best->bearingUncertaintyRadians;
        snapshot.incomingThreatConfidence = best->confidence;
    }
'''
if text.count(old) != 1:
    raise RuntimeError('J3 presentation lifetime anchor mismatch')
p.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')
print('M5-J3 Track snapshot lifetime fix applied')
