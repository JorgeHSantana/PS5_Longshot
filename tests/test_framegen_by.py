"""The console's Frame generation by row: Built-in FG, FSR 3.1 FG, MihawkGen, Lossless Scaling.

The menu had Auto and Built-in only: PS5_MihawkGen's network could not be chosen or turned off there, and Lossless
Scaling's frames were had with a variable in a file (RX_VK_LSFG=1). The row's 2x was forced at every start, over the
player's choice. gpu/vk/fg_by.h says which generator each value asks for; it is compiled here alone, for the PC and
for the console.

python -I -B tests/test_framegen_by.py [--cxx g++]
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

RUN_ALL_ARGS = ["--cxx", "{cxx}"]

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r"""#include "fg_by.h"
using namespace mkwvk::fg_by;
static_assert(kAuto == 0 && kBuiltIn == 1, "the PC's two values keep their numbers: options files have them");
#ifdef __PROSPERO__
static_assert(kMost == kLossless, "the console's row goes up to Lossless Scaling");
static_assert(kLeast == kBuiltIn && kDefault == kMihawk, "the console's row has no Auto: a file's 0 is the network's");
#else
static_assert(kMost == kFsr3 && kLeast == kAuto && kDefault == kAuto, "the PC's row: DLSS FG, Built-in FG, FSR 3.1 FG");
#endif
static_assert(!wants_lossless(kAuto) && !wants_lossless(kBuiltIn) && !wants_lossless(kMihawk) && wants_lossless(kLossless));
static_assert(wants_network(kAuto, false) && wants_network(kMihawk, false), "the network at Auto and at its own value");
static_assert(!wants_network(kBuiltIn, false) && !wants_network(kBuiltIn, true), "Built-in: neither of the two");
static_assert(!wants_network(kLossless, true), "never the network and the chain at once");
static_assert(wants_network(kLossless, false), "no Lossless.dll: the network's frames, not none");
static_assert(level_next(0, 1024, 1024) == 0 && level_next(0, 1023, 1024) == 1, "down to the light one under the line");
static_assert(level_next(1, 512, 1024) == 1 && level_next(1, 511, 1024) == 2, "down to none under half the line");
static_assert(level_next(0, 100, 1024) == 2, "very short at once: none, not a step at a time");
static_assert(level_next(2, 1535, 1024) == 2 && level_next(2, 1536, 1024) == 1, "up from none at one and a half times the line");
constexpr int held(int level, unsigned long long room, int times) {
    int ups = 0;
    for (int i = 0; i < times; i++) level = level_held(level, room, 1024, &ups);
    return level;
}
static_assert(held(2, 4000, kUpHold - 1) == 2 && held(2, 4000, kUpHold) == 1, "a step up only once asked kUpHold times running");
static_assert(held(0, 100, 1) == 2, "a step down at once");
constexpr int swing() {   // (Hyrule Castle: the free memory between 460 and 1130 MiB every few askings)
    int level = 2, ups = 0, changes = 0;
    for (int i = 0; i < 200; i++) {
        const int n = level_held(level, i % 6 < 3 ? 1130 : 460, 1024, &ups);
        changes += n != level, level = n;
    }
    return changes;
}
static_assert(swing() == 0, "memory going up and down does not turn the frames on and off");
static_assert(level_next(2, 9999, 1024) == 1, "up a step at a time");
static_assert(level_next(1, 2047, 1024) == 1 && level_next(1, 2048, 1024) == 0, "up to the row's generator at twice the line");
// (no going back and forth: what a step up takes again leaves it above the line it would go down at)
static_assert(level_next(1, 1024 - 256, 1024) == 1, "the frames' images made again (256 of 1024) do not turn it off again");
static_assert(level_next(0, 2048 - 130, 1024) == 0, "a generator made again does not make it light again");
int main() { return 0; }
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default="g++")
    a = p.parse_args()
    vk = ROOT / "gpu" / "vk"
    with tempfile.TemporaryDirectory(prefix="framegen-by-") as tmp:
        cpp = Path(tmp) / "fixture.cpp"
        cpp.write_text(FIXTURE, encoding="utf-8")
        for define in ([], ["-D__PROSPERO__"]):
            subprocess.run([a.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", "-I", str(vk)] + define +
                           [str(cpp)], check=True)
    framegen = (vk / "vk_framegen.cpp").read_text(encoding="utf-8")
    menu = (vk / "vk_menu.cpp").read_text(encoding="utf-8")
    replay = (vk / "vk_replay.cpp").read_text(encoding="utf-8")
    host = (ROOT / "host" / "hos_main.c").read_text(encoding="utf-8")
    checks = [
        ("r.lo = fg_by::kLeast" in menu, "the menu's row is not the console's values"),
        ("fsr3fi::release();" in framegen[framegen.index("void drop_size() {"):framegen.index("bool by_short")],
         "FSR 3.1's images stay in the console's memory after the row left it"),
        ("release_when_off" not in framegen[framegen.index("bool framegen_on() {"):framegen.index("void framegen_memory_tick() {")]
         and "memory_level_tick" not in framegen[framegen.index("bool framegen_on() {"):framegen.index("void framegen_memory_tick() {")],
         "framegen_on waits for the display thread, which asks it (present_image): the game stops when memory turns it off"),
        (replay.count("framegen_memory_tick();") == 1 and replay.index("framegen_memory_tick();") < replay.index("const bool fg_after = framegen_on()"),
         "the memory step is not once a game's present, before framegen_on"),
        ('"Fill to %d fps"' in menu and '"%dx (game at %d fps)"' in menu,
         "the console's Frame generation values do not say what they do to the game's rate"),
        ("fg_by::level_held(level, room, line, &ups)" in framegen, "frame generation does not step down by the memory left"),
        ("release_when_off()" in framegen, "frame generation off keeps its images"),
        ("framegen_lossless_file() ? fg_by::kMost : fg_by::kMihawk" in menu,
         "without a Lossless.dll the row still offers Lossless Scaling"),
        ("atoi(eq + 1) < fg_by::kLeast) opts.framegen_by = fg_by::kDefault" in menu,
         "a file's Auto is not the network's on the console"),
        ('"MihawkGen FG"' in menu and '"Lossless Scaling FG"' in menu, "the console's generators have no name in the menu"),
        ("if (!fg_by::wants_lossless(fg.by)) return false;" in framegen,
         "Lossless Scaling's chain is not asked for by the row"),
        ("fg.by == by" in framegen, "a change of the row does not make the generators again"),
        ("note_maker(fsr ? FgMaker::Fsr3 : flow ? FgMaker::BuiltIn : who)" in framegen, "Auto's frames by the console's generators are not noted"),
        ('{"RX_VK_FRAMEGEN_MULT", "2"}' not in host, "2x is still forced at every start on the console"),
    ]
    bad = [why for ok, why in checks if not ok]
    if bad:
        raise SystemExit("FAIL: " + "; ".join(bad))
    print("PASS: the console's Frame generation by row: 4 values, one generator at a time, 2x as a default")


if __name__ == "__main__":
    main()
