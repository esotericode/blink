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
    for name in ('teaching.gb','bankdemo.gb','boot.bin','teaching.sym','bankdemo.sym','manifest.json','teaching_rom.hpp'):
        assert (dest/name).read_bytes() == (generated/name).read_bytes(), name
    assert len((dest/'boot.bin').read_bytes())==256
    for name, size, header in (('teaching.gb', 32768, (0x00, 0x00, 0x00)), ('bankdemo.gb', 65536, (0x03, 0x01, 0x02))):
        rom = (dest/name).read_bytes()
        assert len(rom)==size and tuple(rom[0x147:0x14A])==header, name
        checksum=0
        for b in rom[0x134:0x14D]:
            checksum=(checksum-b-1)&255
        assert checksum==rom[0x14D], name
        assert ((sum(rom)-rom[0x14E]-rom[0x14F])&65535)==int.from_bytes(rom[0x14E:0x150],'big'), name
    # Banked code: each bank's routine sits at $4000 inside its own 16 KiB of the file.
    rom = (dest/'bankdemo.gb').read_bytes()
    assert all(rom[bank*0x4000:bank*0x4000+3] == bytes([0x11, 0x0F, 0x40]) for bank in (1, 2, 3))
    assert len({rom[bank*0x4000+0x0F:bank*0x4000+0x1F] for bank in (1, 2, 3)}) == 3
print('PASS deterministic ROMs/boot/header/symbols, cartridge headers and checksums, banked layout')
