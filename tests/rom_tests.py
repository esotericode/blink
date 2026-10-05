"""Rebuild the actual sources twice; compare binary/header/symbol outputs."""
import importlib.util
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('assembler',root/'tools/assemble_rom.py')
assembler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assembler)
generated = Path(sys.argv[1])
with tempfile.TemporaryDirectory(dir=generated.parent) as temporary:
    dest = Path(temporary)
    assembler.build(root/'rom', dest)
    for name in ('teaching.gb','boot.bin','teaching.sym','manifest.json','teaching_rom.hpp'):
        assert (dest/name).read_bytes() == (generated/name).read_bytes(), name
    rom = (dest/'teaching.gb').read_bytes()
    assert len(rom)==32768 and len((dest/'boot.bin').read_bytes())==256
    checksum=0
    for b in rom[0x134:0x14D]:
        checksum=(checksum-b-1)&255
    assert checksum==rom[0x14D]
    assert ((sum(rom)-rom[0x14E]-rom[0x14F])&65535)==int.from_bytes(rom[0x14E:0x150],'big')
print('PASS deterministic ROM/boot/header/symbols and cartridge checksums')
