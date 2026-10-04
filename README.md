# AP27 on Raspberry Pi 5 V3D

This is a vibe coding project under the help of `Codex`

An AP27 search application for PrimeGrid's `ap26` BOINC work units. Its search and result format follow the AP26 application; the compute pipeline runs through Vulkan on Raspberry Pi 5 V3D 7.1.

## Project layout

- `src/` and `include/ap27_v3d/`: BOINC host and V3D search implementation.
- `shaders/`: GLSL compute stages and shared arithmetic.
- `tools/`: sieve-table generator and GPU utilisation helper.
- `boinc/`: anonymous-platform `app_info.xml` template.

## Requirements

- Raspberry Pi 5 with working Mesa V3DV Vulkan compute support.
- CMake 3.31 or newer and a C++20 compiler (tested with GCC 14 and Clang-22).
- Vulkan headers/loader and `glslc`.
- BOINC application development headers and **shared** `libboinc_api` and `libboinc`. Set `-DBOINC_ROOT=/path/to/usr` if CMake cannot find them.

## Build

```bash
git clone https://github.com/void-512/boinc-primegrid-ap27-v3d.git
cd boinc-primegrid-ap27-v3d
mkdir build && cd build
cmake .. -DBOINC_WORK_DIR=$(systemctl show -p WorkingDirectory --value boinc-client)
make -j6
```

The default build creates the complete BOINC payload in `build/bin/`: `ap27_v3d`, six SPIR-V files, the required BOINC shared libraries, `app_info.xml`, and the BOINC copyright notice when available. The shader source is in `shaders/`; the host code is in `src/`.

## Install

Stop the BOINC client completely before replacing project files. BOINC checks application files against sizes recorded when it reads `app_info.xml`; suspending a task is insufficient. From the build directory, install the payload with:

```bash
sudo systemctl stop boinc-client.service
sudo make install
sudo systemctl start boinc-client.service
```

The install target copies the payload to `WorkingDirectory/projects/www.primegrid.com`, with `app_info.xml` last. Check the service's `WorkingDirectory` with `systemctl show boinc-client -p WorkingDirectory` before installing; Check the event log after restarting. The template matches PrimeGrid `ap26` version 2.12 with plan class `cpu_AP27mt`; confirm that this matches the work units assigned to your client.

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

## Standalone run

Run from a writable work directory; the program writes `SOL-AP26.txt` and two `AP26-state.*.txt` checkpoint files there:

```bash
/path/to/build/bin/ap27_v3d KMIN KMAX SHIFT
```

The current implementation keeps one Vulkan device, mapped buffers, descriptor set, six pipelines, command buffer, and fence for the application's lifetime. It uses a 512-lane V3D sieve workgroup and advances residues for the first six small primes. Two adjacent shifts share `n59` generation, residue evolution, and one mask table. The first 48 sieve primes run in the main sieve shader; the remaining 35 run during GPU compaction on surviving records. One submission and fence wait are used per paired tile. Checkpoints keep the same format: an interrupted K is replayed, with that K's partial results rolled back.

The checked-in `include/ap27_v3d/small_primes.hpp` table is the source for the specialized shader fragments. Run `python3 tools/gen_sieve.py` after changing that table, or `python3 tools/gen_sieve.py --check` to verify the generated fragments. The sieve record buffer holds intermediate survivors; `include/ap27_v3d/tile_layout.hpp` names its capacity, the candidate capacity, push constants, and control-buffer words used by the host.