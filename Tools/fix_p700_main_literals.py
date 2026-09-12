from pathlib import Path

p = Path('DeepRun/Main.cpp')
s = p.read_text(encoding='utf-8')
replacements = {
    'std::cout << "[Game][P700] UNDERWATER_BOOSTER_EXIT\n";': 'std::cout << "[Game][P700] UNDERWATER_BOOSTER_EXIT\\n";',
    'std::cout << "[Game][P700] WATER_EXIT\n";': 'std::cout << "[Game][P700] WATER_EXIT\\n";',
    '<< combatFrame->playerP700Impact->explosion.radiusMeters << "\n";': '<< combatFrame->playerP700Impact->explosion.radiusMeters << "\\n";',
    'std::cerr << "[Game][ERROR] P-700 acceptance exceeded 75 s SimulationTime without impact\n";': 'std::cerr << "[Game][ERROR] P-700 acceptance exceeded 75 s SimulationTime without impact\\n";',
}
for old, new in replacements.items():
    if s.count(old) != 1:
        raise RuntimeError(f'expected one match for {old!r}, found {s.count(old)}')
    s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')
print('fixed P-700 smoke string literals')
