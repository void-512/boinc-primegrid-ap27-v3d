# AP27 on Raspberry Pi 5 V3D

This is a pure vibe coding project under the help of `Codex`

An AP27 search application for PrimeGrid's `ap26` BOINC work units. Its search and result format follow the AP26 application; the compute pipeline runs through Vulkan on Raspberry Pi 5 V3D 7.1. A CPU reference path is available with `AP27_BACKEND=cpu`.

## Project layout

- `src/` and `include/ap27_v3d/`: BOINC host and V3D search implementation.
- `shaders/`: GLSL compute stages and shared arithmetic.
- `tests/`: exact V3D tile comparison against the CPU reference.
- `tools/`: sieve-table generator and GPU utilisation helper.
- `boinc/`: anonymous-platform `app_info.xml` template.

## Requirements

- Raspberry Pi 5 with working Mesa V3DV Vulkan compute support (Vulkan 1.2 or newer).
- CMake 3.31 or newer and a C++26 compiler (tested with GCC 14 and Clang-22).
- Vulkan headers/loader and `glslc`.
- BOINC application development headers and **shared** `libboinc_api` and `libboinc`. Set `-DBOINC_ROOT=/path/to/usr` if CMake cannot find them.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The executable, six generated SPIR-V files, and required BOINC shared libraries are placed in `build/bin/`. The shader source and V3D workgroup tuning are in `shaders/`; production host code is in `src/`.

## Standalone run

Run from a writable work directory; the program writes `SOL-AP26.txt` and two `AP26-state.*.txt` checkpoint files there:

```bash
/path/to/build/bin/ap27_v3d KMIN KMAX SHIFT
```

For a short diagnostic that executes one paired GPU tile without committing a result, set `AP27_DIAGNOSTIC_TILE_LIMIT=1`. That diagnostic exits with an error by design. The full search for one K is long.

## BOINC anonymous-platform package

```bash
cmake --build build --target package_boinc
sudo cp build/boinc_package/* /var/lib/boinc-client/projects/www.primegrid.com/
```

`build/boinc_package/` contains `ap27_v3d`, six shaders, the BOINC shared libraries selected at build time, and a generated `app_info.xml`. It also copies a BOINC copyright notice when one is available with the installed libraries. The template matches PrimeGrid `ap26` version 2.12 with plan class `cpu_AP27mt`; confirm that this matches the work units assigned to your client. **Stop the BOINC client completely before replacing any file in the project directory**, install the package with BOINC ownership and permissions, then restart the client and check its event log. Suspending a task is insufficient: BOINC checks application files against sizes recorded when it read `app_info.xml`.

To accept new tasks from BOINC, you need to copy all the files under `build/boinc_package/` to  `/var/lib/boinc-client/projects/www.primegrid.com`, which is the default path for primegrid.

The current implementation keeps one Vulkan device, mapped buffers, descriptor set, six pipelines, command buffer, and fence for the application's lifetime. It uses a 512-lane V3D sieve workgroup and advances residues for the first six small primes. Two adjacent shifts share `n59` generation, residue evolution, and one mask table; compaction and probable-prime checks remain on the GPU. One submission and fence wait are used per paired tile. Checkpoints keep the same format: an interrupted K is replayed, with that K's partial results rolled back.

## Useful BOINC commands

To acquire new task:
```bash
boinccmd --project http://www.primegrid.com/ allowmorework
boinccmd --project http://www.primegrid.com/ update
```

To check current task status:
```bash
boinccmd --get_tasks
```

To stop acquiring new task:
```bash
boinccmd --project http://www.primegrid.com/ nomorework
```

To pause a running task:
```bash
boinccmd --task http://www.primegrid.com/ TASK_NAME suspend
```

To resume a paused task:
```bash
boinccmd --task http://www.primegrid.com/ TASK_NAME resume
```

To cancel a running task:
```bash
boinccmd --task http://www.primegrid.com/ TASK_NAME abort
```
