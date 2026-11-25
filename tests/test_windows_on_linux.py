"""Game-free checks that Longshot's Windows files are made on the Linux runner (docs/dev/reference/ci.md: there is no Windows
one): no workflow asks for a Windows runner, which would queue forever and hold a release; the Linux llvm-mingw that compiles
them is tools.lock's release; the Windows longshot-builder's job checks what it made and the bundle's dav1d (no dav1d is
built in CI); and a tag whose commit CI released already skips only the checks that ship no file.

Run with: python -I tests/test_windows_on_linux.py
"""
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".forgejo" / "workflows"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class WindowsOnLinuxTests(unittest.TestCase):
    def test_no_windows_runner(self):
        for wf in sorted(WORKFLOWS.glob("*.yml")):
            text = wf.read_text(encoding="utf-8")
            with self.subTest(wf.name):
                self.assertIsNone(re.search(r"runs-on:.*windows", text, re.I))
                # (a matrix's os, which runs-on names through ${{ matrix.os }})
                self.assertIsNone(re.search(r"^\s*-?\s*os:\s*windows", text, re.I | re.M))

    def test_linux_llvm_mingw_is_tools_lock_release(self):
        package = module("longshot_build_under_test", ROOT / "scripts" / "longshot_builder.py")
        lock = (ROOT / "tools.lock").read_text(encoding="utf-8").splitlines()
        windows = [l.split()[2] for l in lock if l.startswith("tool llvm-mingw ") and l.split()[3] == "windows-x64"]
        version, prefix, sha256, url = package.linux_llvm_mingw()
        self.assertEqual(len(windows), 1)
        self.assertTrue(windows[0].startswith(version + "-"), (windows, version))
        self.assertEqual(prefix, "llvm-mingw-%s-ucrt-ubuntu-22.04-x86_64" % version)
        self.assertRegex(sha256, r"^[0-9a-f]{64}$")
        self.assertEqual(url, "https://github.com/mstorsjo/llvm-mingw/releases/download/%s/%s.tar.xz" % (version, prefix))

    def test_the_windows_job(self):
        wf = (WORKFLOWS / "build-windows.yml").read_text(encoding="utf-8")
        self.assertIn("scripts/longshot_builder.py build --target windows --profile dist", wf)
        self.assertIn('scripts/longshot_builder.py check "dist/longshot-builder-$TAG-windows-x64.exe"', wf)
        self.assertIn('"dist/longshot-builder-$TAG-windows-x64.exe" sources', wf)   # (started under Wine)
        for name in ("build-windows.yml", "vulkan-dlss.yml"):
            text = (WORKFLOWS / name).read_text(encoding="utf-8")
            with self.subTest(name):
                self.assertNotIn("NUMBER_OF_PROCESSORS", text)   # (Windows' own variable)
                # (each command that runs it, not a comment naming it)
                for line in re.findall(r"^(?![ \t]*#).*longshot_builder\.py llvm-mingw.*", text, re.M):
                    self.assertIn("--host linux", line)

    def test_dav1d_is_the_bundles(self):
        # (dav1d comes from tools.lock's cpm package, in the bundle: CI checks it under Wine and builds none of its own;
        # builder/tools/windows-x64/dav1d.sh makes it, part of the package's recipe)
        wf = (WORKFLOWS / "build-windows.yml").read_text(encoding="utf-8")
        self.assertNotIn("dav1d.sh", wf)
        self.assertNotIn("meson", wf)
        package = wf[wf.index("\n  package:"):]   # (its only job: the Vulkan build check is a step of it)
        self.assertIn('dev tools install "$PWD" --system windows-x64', package)
        self.assertRegex(package, r"tests/test_dav1d_av1\.py --cross \"\$LLVM_MINGW/bin/x86_64-w64-mingw32-clang\" \\\n\s+--run ")
        self.assertLess(package.index("Wine, installed"), package.index("test_dav1d_av1.py"))
        self.assertTrue((ROOT / "builder" / "tools" / "windows-x64" / "dav1d.sh").is_file())

    def test_a_released_commit_skips_only_the_checks(self):
        # (release.yml's checked: a tag's commit CI released already skips the build checks that ship no file; the files,
        # which carry the version, are always built)
        release = (WORKFLOWS / "release.yml").read_text(encoding="utf-8")
        self.assertIn('"$RELEASE_PY" released-as "$ref" --not "$tag"', release)
        self.assertEqual(release.count("checked: ${{ needs.version.outputs.checked }}"), 2)
        windows = (WORKFLOWS / "build-windows.yml").read_text(encoding="utf-8")
        # (the Windows Vulkan build check, once a job of its own, is the package job's last step: the only one gated, the
        # files' steps all run)
        package = windows[windows.index("\n  package:"):]
        gated = re.findall(r"- name: (.*)\n\s+if: env\.RELEASE_CHECKED == ''", package)
        self.assertEqual(gated, ["longshot_vk.dll (build check)"])
        self.assertEqual(package.count("RELEASE_CHECKED"), 1)
        self.assertNotIn("RELEASE_CHECKED", package[:package.index("- name: longshot_vk.dll (build check)")])
        linux = (WORKFLOWS / "build-linux.yml").read_text(encoding="utf-8")
        gated = re.findall(r"- name: (.*)\n\s+if: env\.RELEASE_CHECKED == ''", linux)
        self.assertEqual(gated, ["longshot_vk.so (build check)", "longshot_vk.so with AMD FSR 4 (build check)"])


if __name__ == "__main__":
    unittest.main()
