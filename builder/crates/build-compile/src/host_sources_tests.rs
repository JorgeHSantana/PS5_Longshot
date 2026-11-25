//! The host's C files the game compiles ([`host_sources`]): host/game/'s among them (the frame phases,
//! host/game/phases.c, which tests/test_game_phases.py builds and runs), on both OSes.

use super::*;

#[test]
fn the_game_compiles_host_game_s_files_on_both_oses() {
    let d = crate::testdir("host-sources");
    let (host, posix) = (d.join("host"), d.join("host/posix"));
    for f in ["hos_main.c", "pgo_rt.c", "display.c", "dav1d_av1.c", "hos.h", "game/phases.c", "game/phases.h"] {
        let p = host.join(f);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, "").unwrap();
    }
    let names = |linux, dav1d| -> Vec<String> {
        host_sources(&host, &posix, linux, dav1d)
            .iter()
            .map(|p| p.strip_prefix(&host).unwrap().to_string_lossy().replace('\\', "/"))
            .collect()
    };
    assert_eq!(names(false, false), ["display.c", "hos_main.c", "game/phases.c"]);
    assert_eq!(names(false, true), ["dav1d_av1.c", "display.c", "hos_main.c", "game/phases.c"]);
    assert_eq!(
        names(true, false),
        [
            "hos_main.c",
            "game/phases.c",
            "posix/win32_posix.c",
            "posix/display_sdl.c",
            "posix/linux_stubs.c",
            "posix/prof.c"
        ]
    );
    // (the checkout's own)
    let real = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../host");
    for linux in [false, true] {
        assert!(
            host_sources(&real, &real.join("posix"), linux, false).iter().any(|p| p.ends_with("game/phases.c")),
            "host/game/phases.c is not compiled (linux: {linux})"
        );
    }
}
