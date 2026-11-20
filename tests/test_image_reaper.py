"""Images the renderer lets go of are destroyed on a thread of their own (gpu/vk/vk_vma.cpp destroy_image_later), never while the
textures' compaction has a pass open (after Project ReLink 31689010, 5224e821). No game or GPU required: the source's rules.

- texture_frame's dead images and the render-target pool's trimmed ones go to the reaper, with their views, buffer and memory;
- the reaper frees only while no pass is open, under the lock a pass takes before vmaBeginDefragmentationPass and holds until
  vmaEndDefragmentationPass, so a pass waits for the one free in flight and no free comes inside it; each free outside
  the lock, so the render thread never waits for the queue;
- RX_VK_REAPER=0 destroys them at once, as before.

python -I -B tests/test_image_reaper.py
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def block(text, marker):
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class ImageReaperTests(unittest.TestCase):
    vma = (ROOT / "gpu" / "vk" / "vk_vma.cpp").read_text(encoding="utf-8")
    tex = (ROOT / "gpu" / "vk" / "vk_texture.cpp").read_text(encoding="utf-8")

    def test_the_renderer_hands_its_dead_images_to_the_reaper(self):
        frame = block(self.tex, "void texture_frame(uint64_t completed)")
        loop = frame[frame.index("for (auto it = dead.begin(); it != dead.end();) {"):]
        loop = loop[:loop.index("pool_trim();")]
        self.assertIn("if (compact.in_pass) break;", loop)   # (as before: nothing handed over while a pass is open)
        self.assertIn("destroy_image_later(it->image, it->allocation, std::move(it->views), it->buffer, it->buffer_alloc, "
                      "std::move(it->memory));", loop)
        for inline in ("vkDestroyImageView", "destroy_image(", "destroy_buffer(", "vkFreeMemory"):
            self.assertNotIn(inline, loop.replace("destroy_image_later(", ""), inline)
        self.assertIn("destroy_image_later(p.image, p.allocation, std::move(views));", block(self.tex, "void pool_destroy(Pooled& p)"))

    def test_the_reaper_frees_nothing_while_a_pass_is_open(self):
        reaper = block(self.vma, "void reaper()")
        self.assertIn("reap_cv.wait(lk, [] { return !reap_queue.empty() && !compact_in_pass; });", reaper)
        self.assertLess(reaper.index("std::unique_lock lk(reap_mu);"), reaper.index("reap(r);"))
        # (the free outside the lock, so the render thread hands more over meanwhile; marked in flight under it, and a pass
        # that begins waits for it: held through the queue, destroy_image_later waited for every free before its own)
        order = [reaper.index(x) for x in ("reaping = true;", "lk.unlock();", "reap(r);", "lk.lock();", "reaping = false;",
                                           "reap_done_cv.notify_one();")]
        self.assertEqual(order, sorted(order))
        begin = block(self.vma, "bool image_compact_pass(")
        self.assertLess(begin.index("std::unique_lock rk(reap_mu);"), begin.index("reap_done_cv.wait(rk, [] { return !reaping; });"))
        self.assertLess(begin.index("reap_done_cv.wait(rk, [] { return !reaping; });"), begin.index("vmaBeginDefragmentationPass"))
        self.assertLess(begin.index("vmaBeginDefragmentationPass"), begin.index("compact_in_pass = true;"))
        end = block(self.vma, "bool image_compact_pass_end(")
        locked = end[end.index("std::lock_guard rk(reap_mu);"):end.index("reap_cv.notify_one();")]
        self.assertIn("compact_in_pass = false;", locked)
        self.assertIn("vmaEndDefragmentationPass(vma, compact_ctx, &compact_pass)", locked)

    def test_a_pass_opens_once_the_reaper_has_freed_what_it_was_given(self):
        # (main's rule, compact_step: the images the GPU is done with are freed before the next pass opens; with the reaper they
        # are freed a moment later, so the next pass waits a present for them, never the render thread for the reaper)
        step = block(self.tex, "void compact_step(uint64_t completed)")
        self.assertLess(step.index("if (images_to_reap()) return;"), step.index("image_compact_pass(moves"))
        count = block(self.vma, "size_t images_to_reap()")
        self.assertIn("std::lock_guard lk(reap_mu);", count)
        self.assertIn("reap_queue.size() + (reaping ? 1 : 0)", count)

    def test_the_switch_destroys_at_once(self):
        later = block(self.vma, "void destroy_image_later(")
        self.assertLess(later.index("if (!reaper_on()) {"), later.index("reap_queue.push_back"))
        self.assertIn('env_on_or("RX_VK_REAPER", 1)', block(self.vma, "bool reaper_on()"))
        # (no thread: destroyed where it was handed over, as before)
        self.assertLess(later.index("catch (...)"), later.index("reap_queue.push_back"))
        self.assertIn("SW(RX_VK_REAPER, kill, bool,", (ROOT / "gpu" / "vk" / "switches.def").read_text(encoding="utf-8"))

    def test_the_reaper_keeps_the_memory_counts(self):
        # (destroy_image and destroy_buffer take an image's and a buffer's bytes off mem_account.h's counts: a vmaDestroyImage
        # or vmaDestroyBuffer here would leave every reaped image in the budget line's "other images" for good)
        reap = block(self.vma, "void reap(Reaped& r)")
        for call in ("vkDestroyImageView", "destroy_image(r.image, r.allocation)", "destroy_buffer(r.buffer, r.buffer_alloc)",
                     "vkFreeMemory"):
            self.assertIn(call, reap)
        for raw in ("vmaDestroyImage", "vmaDestroyBuffer"):
            self.assertNotIn(raw, reap)


if __name__ == "__main__":
    unittest.main()
