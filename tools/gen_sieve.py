#!/usr/bin/env python3
"""Generate identical small-prime tables for the CPU oracle and Vulkan sieve."""
from pathlib import Path
import argparse
import re

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--upstream-sieve", type=Path,
                    default=root / ".local/sources/ap27/opencl/kernels/sieve.cl",
                    help="path to the upstream AP27 opencl/kernels/sieve.cl")
args = parser.parse_args()
source = args.upstream_sieve.read_text()
pairs = [(int(p), int(c or 1)) for c, p in re.findall(
    r"n59a\+(?:(\d+)\*)?n59b\)%(\d+)", source)]
assert len(pairs) == 83 and pairs[0] == (61, 60) and pairs[-1] == (541, 189)
assert all(pow(2, 30, p) == c for p, c in pairs)
offset = 0
entries = []
for p, c in pairs:
    entries.append((p, c, offset))
    offset += p
assert offset == 23693

header_out = root / "include/ap27_v3d"
shader_out = root / "shaders"
header = ["#pragma once", "#include <array>", "#include <cstdint>",
          "struct SmallPrime { uint32_t p, coeff, offset; };",
          "inline constexpr std::array<SmallPrime, 83> small_primes = {{"]
header += [f"    {{{p}u, {c}u, {o}u}}," for p, c, o in entries]
header += ["}};", "inline constexpr uint32_t mask_count = 23693;"]
(header_out / "small_primes.hpp").write_text("\n".join(header) + "\n")

body = ["uvec2 sieve_mask(uint a, uint b) {",
        "    uvec2 m=uvec2(0xffffffffu);"]
for i, (p, c, o) in enumerate(entries):
    term = f"(a+{c}u*b)%{p}u"
    if o:
        term += f"+{o}u"
    body.append(f"    m &= mask_data.m[{term}];")
    if (i + 1) % 5 == 0 and i + 1 < len(entries):
        body.append("    if((m.x|m.y)==0u) return m;")
body += ["    return m;", "}"]
(shader_out / "sieve_mask.glsl").write_text("\n".join(body) + "\n")
tail = ["uvec2 sieve_mask_tail(uint a, uint b, uvec2 m) {"]
for i, (p, c, o) in enumerate(entries[5:], start=5):
    term = f"(a+{c}u*b)%{p}u"
    if o:
        term += f"+{o}u"
    tail.append(f"    m &= mask_data.m[{term}];")
    if (i + 1) % 5 == 0 and i + 1 < len(entries):
        tail.append("    if((m.x|m.y)==0u) return m;")
tail += ["    return m;", "}"]
(shader_out / "sieve_mask_tail.glsl").write_text("\n".join(tail) + "\n")
print(f"generated {len(entries)} primes, {offset} mask words")
