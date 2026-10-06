#!/usr/bin/env python3
"""Strict two-pass assembler for the documented teaching-ROM subset (MIT).

This is a build tool, not an emulator. Unsupported syntax fails with a source
line. Produces deterministic ROM/boot/symbols and a C++ embedding header.

Supported: DEF name EQU value, labels, ORG, BANK n (subsequent ORG $4000-$7FFF
lands in 16 KiB ROM bank n of the file), DB, and the SM83 instructions in
encode() below. Numbers are decimal or $hex; operands may be symbols.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

R8 = ['b', 'c', 'd', 'e', 'h', 'l', '[hl]', 'a']
R16 = ['bc', 'de', 'hl', 'sp']
STACK16 = ['bc', 'de', 'hl', 'af']
CONDITIONS = {'nz': 0, 'z': 1, 'nc': 2, 'c': 3}
ALU = {'add a,': 0, 'adc a,': 1, 'sub': 2, 'sbc a,': 3, 'and': 4, 'xor': 5, 'or': 6, 'cp': 7}
FIXED = {'nop': [0x00], 'halt': [0x76], 'di': [0xF3], 'ei': [0xFB], 'ret': [0xC9], 'reti': [0xD9],
         'rlca': [0x07], 'rrca': [0x0F], 'rla': [0x17], 'rra': [0x1F], 'daa': [0x27], 'cpl': [0x2F],
         'scf': [0x37], 'ccf': [0x3F], 'jp hl': [0xE9], 'ld sp, hl': [0xF9],
         'ld [bc], a': [0x02], 'ld [de], a': [0x12], 'ld [hl+], a': [0x22], 'ld [hl-], a': [0x32],
         'ld a, [bc]': [0x0A], 'ld a, [de]': [0x1A], 'ld a, [hl+]': [0x2A], 'ld a, [hl-]': [0x3A]}


def assemble(path, size):
    """Return (image, symbols, banks): label/constant addresses and each label's ROM bank."""
    symbols = {}
    banks = {}
    lines = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = re.sub(r'\s+', ' ', raw.split(';', 1)[0].strip().lower())
        if line:
            lines.append((number, line))

    def val(s, unresolved=False):
        s = s.strip()
        if s.startswith('$'):
            return int(s[1:], 16)
        if re.fullmatch(r'-?\d+', s):
            return int(s)
        if s in symbols:
            return symbols[s]
        if unresolved:
            return 0
        raise ValueError(f'unknown symbol {s}')

    def encode(line, pc, first):
        def n(s):
            return val(s, first)
        def byte(v):
            if not 0 <= v <= 255:
                raise ValueError(f'byte out of range: {v}')
            return [v]
        def word(op, s):
            v = n(s)
            if not 0 <= v <= 65535:
                raise ValueError(f'word out of range: {v}')
            return [op, v & 255, v >> 8]
        if line in FIXED:
            return FIXED[line]
        if line.startswith('db '):
            return sum((byte(n(v)) for v in line[3:].split(',')), [])
        if line.startswith('jr '):
            args = [x.strip() for x in line[3:].split(',')]
            op = 0x18 if len(args) == 1 else 0x20 + CONDITIONS[args[0]] * 8
            offset = 0 if first else n(args[-1]) - pc - 2
            if not -128 <= offset <= 127:
                raise ValueError('relative jump out of range')
            return [op, offset & 255]
        m = re.fullmatch(r'(jp|call) (?:(nz|z|nc|c), )?(.+)', line)
        if m:
            kind, condition, target = m.groups()
            if condition is None:
                return word(0xC3 if kind == 'jp' else 0xCD, target)
            return word((0xC2 if kind == 'jp' else 0xC4) + CONDITIONS[condition] * 8, target)
        m = re.fullmatch(r'ret (nz|z|nc|c)', line)
        if m:
            return [0xC0 + CONDITIONS[m[1]] * 8]
        m = re.fullmatch(r'(push|pop) (bc|de|hl|af)', line)
        if m:
            return [(0xC5 if m[1] == 'push' else 0xC1) + STACK16.index(m[2]) * 16]
        m = re.fullmatch(r'(inc|dec) (bc|de|hl|sp)', line)
        if m:
            return [(0x03 if m[1] == 'inc' else 0x0B) + R16.index(m[2]) * 16]
        m = re.fullmatch(r'(inc|dec) (b|c|d|e|h|l|\[hl\]|a)', line)
        if m:
            return [(0x04 if m[1] == 'inc' else 0x05) + R8.index(m[2]) * 8]
        m = re.fullmatch(r'ld (bc|de|hl|sp), (.+)', line)
        if m:
            return word(0x01 + R16.index(m[1]) * 16, m[2])
        m = re.fullmatch(r'ld (b|c|d|e|h|l|\[hl\]|a), (b|c|d|e|h|l|\[hl\]|a)', line)
        if m:
            if m[1] == m[2] == '[hl]':
                raise ValueError('ld [hl], [hl] is HALT')
            return [0x40 + R8.index(m[1]) * 8 + R8.index(m[2])]
        m = re.fullmatch(r'ld a, \[(.+)\]', line)
        if m:
            return word(0xFA, m[1])
        m = re.fullmatch(r'ld \[(.+)\], a', line)
        if m:
            return word(0xEA, m[1])
        m = re.fullmatch(r'ld (b|c|d|e|h|l|\[hl\]|a), (.+)', line)
        if m:
            return [0x06 + R8.index(m[1]) * 8] + byte(n(m[2]))
        m = re.fullmatch(r'ldh (a, \[(.+)\]|\[(.+)\], a)', line)
        if m:
            addr = n(m[2] or m[3])
            if not 0xFF00 <= addr <= 0xFFFF:
                raise ValueError('LDH requires an FF00-FFFF address')
            return [0xF0 if m[2] else 0xE0, addr & 255]
        m = re.fullmatch(r'(add a,|adc a,|sub|sbc a,|and|xor|or|cp) (.+)', line)
        if m:
            operand = m[2].strip()
            if operand in R8:
                return [0x80 + ALU[m[1]] * 8 + R8.index(operand)]
            return [0xC6 + ALU[m[1]] * 8] + byte(n(operand))
        m = re.fullmatch(r'(bit|res|set) ([0-7]), (b|c|d|e|h|l|\[hl\]|a)', line)
        if m:
            return [0xCB, {'bit': 0x40, 'res': 0x80, 'set': 0xC0}[m[1]] + int(m[2]) * 8 + R8.index(m[3])]
        m = re.fullmatch(r'swap (b|c|d|e|h|l|\[hl\]|a)', line)
        if m:
            return [0xCB, 0x30 + R8.index(m[1])]
        raise ValueError(f'unsupported syntax: {line}')

    image = bytearray(size)
    occupied = set()
    for first in (True, False):
        pc = 0
        bank = 0
        for number, line in lines:
            try:
                m = re.fullmatch(r'def (\w+) equ (.+)', line)
                if m:
                    if first:
                        if m[1] in symbols:
                            raise ValueError('duplicate symbol')
                        symbols[m[1]] = val(m[2])
                    continue
                if line.endswith(':'):
                    if first:
                        name = line[:-1]
                        if name in symbols:
                            raise ValueError('duplicate label')
                        symbols[name] = pc
                        banks[name] = bank
                    continue
                if line.startswith('org '):
                    pc = val(line[4:])
                    continue
                if line.startswith('bank '):
                    bank = val(line[5:])
                    if not 1 <= bank < size // 0x4000:
                        raise ValueError('bank outside the ROM')
                    continue
                data = encode(line, pc, first)
                if bank:
                    # Banked code is addressed at $4000-$7FFF but stored at bank * 16 KiB.
                    if not 0x4000 <= pc <= 0x8000 - len(data):
                        raise ValueError('banked output must lie in $4000-$7FFF')
                    offset = bank * 0x4000 + pc - 0x4000
                else:
                    offset = pc
                if not 0 <= offset <= size - len(data):
                    raise ValueError('output exceeds ROM size')
                if not first:
                    for address in range(offset, offset + len(data)):
                        if address in occupied:
                            raise ValueError('overlapping output')
                        occupied.add(address)
                    image[offset:offset+len(data)] = bytes(data)
                pc += len(data)
            except (ValueError, KeyError) as error:
                raise ValueError(f'{path}:{number}: {error}') from error
    return image, symbols, banks


def finish_header(rom, title, cartridge, rom_size, ram_size):
    """Write title, cartridge type, sizes, and both checksums (Pan Docs "The Cartridge Header")."""
    rom[0x134:0x143] = title.ljust(15, b'\0')
    rom[0x147:0x14A] = bytes([cartridge, rom_size, ram_size])
    rom[0x14A] = 1
    checksum = 0
    for b in rom[0x134:0x14D]:
        checksum = (checksum - b - 1) & 255
    rom[0x14D] = checksum
    total = sum(rom) & 65535
    rom[0x14E:0x150] = bytes([total >> 8, total & 255])


# Original cartridges built from rom/. Header bytes: cartridge type, ROM size, RAM size.
CARTRIDGES = {
    'teaching': dict(size=32768, title=b'OBSERVATORY', cartridge=0x00, rom_size=0x00, ram_size=0x00, namespace='demo'),
    'bankdemo': dict(size=65536, title=b'BANK DEMO', cartridge=0x03, rom_size=0x01, ram_size=0x02, namespace='bankdemo'),
}


def build(source, output):
    output.mkdir(parents=True, exist_ok=True)
    boot, _, _ = assemble(source / 'boot.asm', 256)
    built = {}
    for name, spec in CARTRIDGES.items():
        rom, symbols, banks = assemble(source / f'{name}.asm', spec['size'])
        finish_header(rom, spec['title'], spec['cartridge'], spec['rom_size'], spec['ram_size'])
        (output / f'{name}.gb').write_bytes(rom)
        (output / f'{name}.sym').write_text(''.join(
            f'{banks.get(k, 0):02X}:{v:04X} {k}\n'
            for k, v in sorted(symbols.items(), key=lambda x: (banks.get(x[0], 0), x[1]))))
        built[name] = (rom, symbols)
    (output / 'boot.bin').write_bytes(boot)
    manifest = {'boot_sha256': hashlib.sha256(boot).hexdigest()}
    for name, (rom, symbols) in built.items():
        manifest[f'{name}_sha256'] = hashlib.sha256(rom).hexdigest()
        manifest[f'{name}_symbols'] = symbols
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    def array(name, data):
        rows = ['    ' + ','.join(f'0x{x:02X}' for x in data[i:i+32]) for i in range(0, len(data), 32)]
        return f'inline constexpr std::array<std::uint8_t, {len(data)}> {name} = {{\n' + ',\n'.join(rows) + '\n};\n'
    header = '#pragma once\n#include <array>\n#include <cstdint>\n'
    for name, (rom, symbols) in built.items():
        header += f'namespace observatory::{CARTRIDGES[name]["namespace"]} {{\n' + array('rom', rom)
        if name == 'teaching':
            header += array('boot', boot)
        for k, v in symbols.items():
            header += f'inline constexpr std::uint16_t {k} = 0x{v:04X};\n'
        header += '}\n'
    (output / 'teaching_rom.hpp').write_text(header)
    for name, (rom, _) in built.items():
        print(f'Built {name}.gb: {hashlib.sha256(rom).hexdigest()}')
    print(f'Built boot.bin: {hashlib.sha256(boot).hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'rom')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
