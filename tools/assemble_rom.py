#!/usr/bin/env python3
"""Strict two-pass assembler for the documented teaching-ROM subset (MIT).

This is a build tool, not an emulator. Unsupported syntax fails with a source
line. Produces deterministic ROM/boot/symbols and a C++ embedding header.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def assemble(path, size):
    symbols = {}
    lines = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split(';', 1)[0].strip().lower()
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
        fixed = {'nop':[0], 'di':[0xF3], 'xor a':[0xAF], 'or a':[0xB7],
                 'or c':[0xB1], 'cpl':[0x2F], 'inc a':[0x3C], 'dec a':[0x3D],
                 'inc d':[0x14], 'inc de':[0x13], 'dec bc':[0x0B],
                 'dec b':[0x05], 'ld a, b':[0x78], 'ld a, d':[0x7A],
                 'ld a, [de]':[0x1A], 'ld [hl+], a':[0x22]}
        if line in fixed:
            return fixed[line]
        if line.startswith('db '):
            return sum((byte(n(v)) for v in line[3:].split(',')), [])
        if line.startswith('jp '):
            return word(0xC3, line[3:])
        if line.startswith('jr '):
            args = [x.strip() for x in line[3:].split(',')]
            op = 0x18 if len(args) == 1 else {'nz':0x20,'z':0x28,'nc':0x30,'c':0x38}[args[0]]
            offset = 0 if first else n(args[-1]) - pc - 2
            if not -128 <= offset <= 127:
                raise ValueError('relative jump out of range')
            return [op, offset & 255]
        m = re.fullmatch(r'ld (a|b|d|bc|de|hl|sp), (.+)', line)
        if m:
            reg, operand = m.groups()
            if reg == 'a' and operand.startswith('['):
                return word(0xFA, operand[1:-1])
            if reg in ('bc','de','hl','sp'):
                return word({'bc':0x01,'de':0x11,'hl':0x21,'sp':0x31}[reg], operand)
            return [{'a':0x3E,'b':0x06,'d':0x16}[reg]] + byte(n(operand))
        m = re.fullmatch(r'ld \[(.+)\], a', line)
        if m:
            return word(0xEA, m[1])
        m = re.fullmatch(r'ldh (a, \[(.+)\]|\[(.+)\], a)', line)
        if m:
            addr = n(m[2] or m[3])
            if not 0xFF00 <= addr <= 0xFFFF:
                raise ValueError('LDH requires an FF00-FFFF address')
            return [0xF0 if m[2] else 0xE0, addr & 255]
        m = re.fullmatch(r'(and|cp|add a,) (.+)', line)
        if m:
            return [{'and':0xE6,'cp':0xFE,'add a,':0xC6}[m[1]]] + byte(n(m[2]))
        m = re.fullmatch(r'bit ([0-7]), a', line)
        if m:
            return [0xCB, 0x47 + int(m[1]) * 8]
        raise ValueError(f'unsupported syntax: {line}')

    image = bytearray(size)
    occupied = set()
    for first in (True, False):
        pc = 0
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
                    continue
                if line.startswith('org '):
                    pc = val(line[4:])
                    continue
                data = encode(line, pc, first)
                if not 0 <= pc <= size - len(data):
                    raise ValueError('output exceeds ROM size')
                if not first:
                    for address in range(pc, pc + len(data)):
                        if address in occupied:
                            raise ValueError('overlapping output')
                        occupied.add(address)
                    image[pc:pc+len(data)] = bytes(data)
                pc += len(data)
            except (ValueError, KeyError) as error:
                raise ValueError(f'{path}:{number}: {error}') from error
    return image, symbols


def build(source, output):
    output.mkdir(parents=True, exist_ok=True)
    rom, symbols = assemble(source / 'teaching.asm', 32768)
    boot, _ = assemble(source / 'boot.asm', 256)
    rom[0x134:0x143] = b'OBSERVATORY'.ljust(15, b'\0')
    rom[0x147:0x14A] = bytes([0, 0, 0])  # ROM-only, 32 KiB, no cart RAM
    rom[0x14A] = 1
    checksum = 0
    for b in rom[0x134:0x14D]:
        checksum = (checksum - b - 1) & 255
    rom[0x14D] = checksum
    total = sum(rom) & 65535
    rom[0x14E:0x150] = bytes([total >> 8, total & 255])
    (output / 'teaching.gb').write_bytes(rom)
    (output / 'boot.bin').write_bytes(boot)
    (output / 'teaching.sym').write_text(''.join(f'00:{v:04X} {k}\n' for k,v in sorted(symbols.items(), key=lambda x:x[1])))
    (output / 'manifest.json').write_text(json.dumps({'teaching_sha256':hashlib.sha256(rom).hexdigest(),
                                                   'boot_sha256':hashlib.sha256(boot).hexdigest(),
                                                   'symbols':symbols}, indent=2) + '\n')
    def array(name, data):
        rows = ['    ' + ','.join(f'0x{x:02X}' for x in data[i:i+32]) for i in range(0,len(data),32)]
        return f'inline constexpr std::array<std::uint8_t, {len(data)}> {name} = {{\n' + ',\n'.join(rows) + '\n};\n'
    header = '#pragma once\n#include <array>\n#include <cstdint>\nnamespace observatory::demo {\n'
    header += array('rom', rom) + array('boot', boot)
    for k,v in symbols.items():
        header += f'inline constexpr std::uint16_t {k} = 0x{v:04X};\n'
    header += '}\n'
    (output / 'teaching_rom.hpp').write_text(header)
    print(f'Built teaching.gb: {hashlib.sha256(rom).hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'rom')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
