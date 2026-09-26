# AP27 on Raspberry Pi 5 V3D

An AP27 search application for PrimeGrid's `ap26` BOINC work units. Its search and result format follow the AP26 application; the compute pipeline runs through Vulkan on Raspberry Pi 5 V3D 7.1. A CPU reference path is available with `AP27_BACKEND=cpu`.

**Status:** the V3D tile pipeline matches the CPU reference on all 10 shifts of the `K=366384` test tile. The first real PrimeGrid work unit failed at BOINC startup after an application-file update while the client was running; see [the incident report](docs/boinc-file-size-incident.md). Full validator acceptance remains pending. See [LICENSE.md](LICENSE.md) before publishing or redistributing this derived code.

## Project layout

- `src/` and `include/ap27_v3d/`: BOINC host and V3D search implementation.
- `shaders/`: GLSL compute stages and shared arithmetic.
- `tests/`: exact V3D tile comparison against the CPU reference.
- `tools/`: sieve-table generator and GPU utilisation helper.
- `boinc/`: anonymous-platform `app_info.xml` template.

Historical experiments, downloaded source trees, prompts, reports, and old packages from development on this Pi are preserved in ignored `.local/`. They are not needed in a source checkout. The local BOINC client installation is separate from this repository.

The generated sieve tables are checked in. To regenerate them, run `python3 tools/gen_sieve.py --upstream-sieve /path/to/ap27/opencl/kernels/sieve.cl` with the upstream AP27 source; the local archive supplies that input by default on this Pi.

## Requirements

- Raspberry Pi 5 with working Mesa V3DV Vulkan compute support (Vulkan 1.2 or newer).
- CMake 3.31 or newer and a C++26 compiler (tested with GCC 14).
- Vulkan headers/loader and `glslc`.
- BOINC application development headers and **shared** `libboinc_api` and `libboinc`. Set `-DBOINC_ROOT=/path/to/usr` if CMake cannot find them. A local extracted BOINC package under `.local/sources/boinc-packages/root/usr` is recognized for development, but is not part of the source release.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --build build --target ap27_tile_test -j
ctest --test-dir build --output-on-failure
```

The test requires direct access to the V3D device. Use a separate build directory with `-DCMAKE_BUILD_TYPE=Debug` for a Debug build. On a Pi with RAM-backed `~/.cache`, a build directory there avoids unnecessary storage writes.

The executable, six generated SPIR-V files, and required BOINC shared libraries are placed in `build/bin/`. The shader source and V3D workgroup tuning are in `shaders/`; production host code is in `src/`.

## Standalone run

Run from a writable work directory; the program writes `SOL-AP26.txt` and two `AP26-state.*.txt` checkpoint files there:

```bash
/path/to/build/bin/ap27_v3d KMIN KMAX SHIFT
```

For a short diagnostic that executes one tile without committing a result, set `AP27_DIAGNOSTIC_TILE_LIMIT=1`. That diagnostic exits with an error by design. The full search for one K is long.

## BOINC anonymous-platform package

```bash
cmake --build build --target package_boinc
```

`build/boinc_package/` contains `ap27_v3d`, six shaders, the BOINC shared libraries selected at build time, and a generated `app_info.xml`. It also copies a BOINC copyright notice when one is available with the installed libraries. The template matches PrimeGrid `ap26` version 2.12 with plan class `cpu_AP27mt`; confirm that this matches the work units assigned to your client. **Stop the BOINC client completely before replacing any file in the project directory**, install the package with BOINC ownership and permissions, then restart the client and check its event log. Suspending a task is insufficient: BOINC checks application files against sizes recorded when it read `app_info.xml`. Never copy account keys, client state, task slots, or checkpoint files from another host. The package target only stages files; it does not alter a running BOINC installation.

The current implementation keeps one Vulkan device, mapped buffers, descriptor set, six pipelines, command buffer, and fence for the application's lifetime. It uses a 512-lane V3D sieve workgroup and advances residues for the first five small primes. One submission and fence wait are used per tile so the existing checkpoint boundary remains clear.

## Useful BOINC commands

To acquire new task:
```bash
sudo -u boinc boinccmd --project http://www.primegrid.com/ allowmorework
sudo -u boinc boinccmd --project http://www.primegrid.com/ update
sudo -u boinc boinccmd --get_tasks
```

To stop acquiring new task:
```bash
boinccmd --project http://www.primegrid.com/ nomorework
```
