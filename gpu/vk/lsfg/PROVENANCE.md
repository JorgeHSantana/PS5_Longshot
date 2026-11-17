# gpu/vk/lsfg

Lossless Scaling's frame generation (LSFG) on plain Vulkan and VMA, 26 files copied from
[PS5SX2](https://github.com/Swordpdf/PS5SX2) `ps5/coreorbis/orbis-shims/lsfg` at 5d94db4e73 (GPL-3.0-or-later; copyright and
SPDX lines kept). PS5SX2's is the Eden emulator's (git.eden-emu.dev, 67bada77f8a4), which is lsfg-vk's.

Nothing of Lossless Scaling is here: its compute shaders are read at run time from the player's own `Lossless.dll`.

Changed here: `lsfg_vk.h` takes Vulkan from Longshot's loader (`../vk_loader.h`) under `LONGSHOT_LSFG`. `lsfg_backend.h/.cpp`: `RecordShared` and `RecordGeneration` (a frame at the chain's moment nearest to the one asked,
for Auto and for 3x and 4x), and every generation slot's target is the one output. Nothing else.
The adapter is in `gpu/vk/vk_framegen.cpp` (`lsfg_feed`, `lsfg_take`); `ps5/build.py` compiles the folder.
