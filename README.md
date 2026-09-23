<p align="center"><img src="https://raw.githubusercontent.com/melonDS-emu/melonDS/master/res/icon/melon_128x128.png"></p>
<h2 align="center"><b>melonDS — PokéBot Lua Fork</b></h2>

<p align="center">
<a href="https://www.gnu.org/licenses/gpl-3.0" alt="License: GPLv3"><img src="https://img.shields.io/badge/License-GPL%20v3-%23ff554d.svg"></a>
</p>

An unofficial fork of **melonDS**, adapted for use with **PokéBot NDS** and Lua-driven Nintendo DS automation.

The aim of this fork is to retain melonDS compatibility and performance while providing the Lua behaviour and emulator controls needed for long-running PokéBot automation.

This fork is based on the original [melonDS](https://github.com/melonDS-emu/melonDS) project. It is not an official melonDS release and is not maintained or endorsed by the melonDS developers.

## Main Changes

### PokéBot / Lua support

This fork contains the Lua integration and compatibility changes used by the modified PokéBot NDS project.

The Lua environment allows PokéBot to interact with the running game, read game state, send controller inputs and perform automated tasks.

### Performance

The Windows Release build used with this fork is built with the LLVM/Clang toolchain and melonDS JIT support.

Release builds use link-time optimisation where supported by the existing build configuration. The project does not rely on CPU-specific compilation, allowing builds to remain suitable for different supported processors.

The actual emulation speed obtained depends on the game, workload, emulator configuration and host system.

For a detailed record of fork-specific source changes, see [`MODIFICATIONS.md`](MODIFICATIONS.md).

## How to use

Firmware boot (not direct boot) requires a BIOS/firmware dump from an original DS or DS Lite.

DS firmwares dumped from a DSi or 3DS aren't bootable and only contain configuration data, so they are only suitable when booting games directly.

### Possible firmware sizes

- 128KB: DSi/3DS DS-mode firmware (reduced size due to lacking bootcode)
- 256KB: regular DS firmware
- 512KB: iQue DS firmware

DS BIOS dumps from a DSi or 3DS can be used for DS-mode emulation.

Users must provide their own legally obtained games, BIOS and firmware. These are not included with this project.

## Using with PokéBot NDS

1. Build or obtain the matching build of this melonDS fork.
2. Configure melonDS for the Nintendo DS game you want to automate.
3. Start the PokéBot NDS dashboard.
4. Open melonDS's Lua scripting interface and load `pokebot-nds.lua` from the compatible PokéBot NDS fork.
5. Configure the required PokéBot mode from its dashboard.
6. Enable Focus Mode when you want to prioritise automation throughput over normal game presentation.

PokéBot NDS is maintained separately from this emulator fork and is not part of the melonDS source tree.

## How to build

The upstream build documentation is retained in [`BUILD.md`](./BUILD.md).

For the Windows build used during development of this fork, the project is configured as a Release build with **Clang/LLVM**, **Ninja**, **vcpkg**, and the existing melonDS JIT support.

Example configuration:

```bat
cd /d "C:\path\to\melonDS-PokeBot-Lua"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

rmdir /s /q build-clang
mkdir build-clang
cd build-clang

cmake .. -G Ninja ^
-DUSE_VCPKG=ON ^
-DCMAKE_C_COMPILER=clang-cl ^
-DCMAKE_CXX_COMPILER=clang-cl ^
-DCMAKE_BUILD_TYPE=Release ^
-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
-DVCPKG_TARGET_TRIPLET=x64-windows ^
-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL

cmake --build . -j 8
```

The exact Visual Studio path may differ depending on the installed version and edition.

See [`BUILD.md`](./BUILD.md) for the full upstream build requirements and platform-specific instructions.

## Upstream melonDS

melonDS is a Nintendo DS emulator originally developed by the melonDS project.

Official upstream resources:

- [melonDS GitHub repository](https://github.com/melonDS-emu/melonDS)
- [melonDS website](https://melonds.kuribo64.net/)

Issues caused specifically by modifications in this fork should not be reported to the upstream melonDS project.

## Credits

Original melonDS credits are retained:

- Martin for GBAtek, a good piece of documentation
- Cydrak for the extra 3D GPU research
- limittox for the icon
- All contributors, testers and users who have helped develop and improve melonDS

All original source attribution and copyright notices remain with their respective authors.

## License

[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.en.html)

melonDS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This modified fork is distributed under the same GPLv3-or-later terms.

The complete corresponding source for releases of this fork should be made available with the project so that modified binaries can be rebuilt from source.

### External

- Images used in the Input Config Dialog — see `src/frontend/qt_sdl/InputConfig/resources/LICENSE.md`
