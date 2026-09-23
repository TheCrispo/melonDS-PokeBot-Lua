# Modifications

This repository is an unofficial modified fork of melonDS intended for
use with PokéBot NDS and Lua-driven Nintendo DS automation.

The original melonDS project, its authors, and its contributors remain
credited under the project's existing GPLv3-or-later licensing and
source-file notices.

## Lua scripting support

The fork adds a Lua scripting layer used by PokéBot NDS. The exposed
compatibility APIs include:

-   Main-memory reads and writes.
-   Joypad input reading and control.
-   Stylus input.
-   Frame advance, frame count, reset, and emulation-state helpers.
-   ROM information helpers.
-   Savestate save/load helpers.
-   Lua `print` output through the emulator.
-   Bit-operation compatibility helpers.
-   Socket communication required for the PokéBot dashboard.

The socket implementation supports the message framing used by the
PokéBot NDS dashboard.

## Scope

These modifications are intended to support the accompanying PokéBot NDS
workflow while keeping the emulator project separate from PokéBot
itself.

PokéBot scripts, Nintendo DS ROMs, user save data, retail BIOS dumps,
and retail firmware are not part of this source repository.

## Upstream

Original project:

https://github.com/melonDS-emu/melonDS

This fork is unofficial. Fork-specific issues should be reported to the
maintainer of this fork rather than to the upstream melonDS project
unless they can also be reproduced on an unmodified upstream build.
