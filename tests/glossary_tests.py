"""Every tooltip key the interface uses exists in the glossary, and entries stay short."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
glossary = (root / 'src/teaching/glossary.cpp').read_text(encoding='utf-8')
entries = dict(re.findall(r'\{"([a-z]+\.[a-z0-9.\-]+)", \{"([^"]+)"', glossary))
assert len(entries) > 60, f'glossary unexpectedly small: {len(entries)}'

families = 'panel|action|ui|reg|flag|cpu|part|path|memory|cart|tiles|map|writes|info'
used = set()
for source in list((root / 'src/ui').glob('*.cpp')):
    text = source.read_text(encoding='utf-8')
    used |= {m for m in re.findall(rf'"((?:{families})\.[a-z0-9.\-]+)"', text)}
    # Docks derive their key from the object name: makeDock("Title", "memoryDock") -> "panel.memory".
    used |= {f'panel.{name}' for name in re.findall(r'makeDock\("[^"]*", "(\w+)Dock"', text)}
missing = sorted(key for key in used if key not in entries)
assert not missing, f'tooltip keys missing from the glossary: {missing}'

# Bodies: plain sentences a newcomer can read in a few seconds.
for key, body in re.findall(r'\{"([a-z]+\.[a-z0-9.\-]+)", \{"[^"]+",\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', glossary):
    text = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', body))
    assert text.strip(), f'{key} has no explanation'
    assert len(text) <= 380, f'{key} explanation is too long ({len(text)} characters)'
print(f'PASS glossary: {len(entries)} entries, {len(used)} keys used by the interface, all present and concise')
