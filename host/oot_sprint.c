/* (longshot) Sprint (the Controls tab's Sprint row, the Controls page's Sprint line and its Toggle; host/nn_hid.c reads
   them): Link sprints while his input flags hold the dash input (bit 4 of the word at *(Link +0x50)), the flag the game sets
   itself for the run that follows a roll. Two places load those flags (nx2/oot_patches.py patch_sprint_link and
   patch_sprint_stance): Link's movement update, f_804caa80 at 0x804ce798, for the sprint's start and keep tests, and the
   stance and camera update, f_80c225d0 at 0x80c27f3c. Right after each load they call hos_sprint_link and
   hos_sprint_stance below, which OR the bit in while Sprint is on, so the game's own sprint runs: its speed, animation,
   camera and every rule that ends it. A is never touched: the player's own A (a roll, talking, opening, picking up) is
   theirs whether Sprint runs or not.

   Toggle off (the default): Link sprints while Sprint's key or button is held.
   Toggle on: a press starts the sprint, the next press or Link's state ends it. A toggled sprint goes on through jumps,
   slides, rolls and landings (a roll keeps it, and it goes on when the roll ends) and ends when Link's state is anything
   else (an attack, an item, swimming, climbing, a cutscene), when ZL is held, or after WAIT_STOP_FRAMES 60 Hz frames
   standing still (WAIT_START_FRAMES after a press made while standing, before Link has moved), at any frame rate. Opening
   Insert, Plus or Minus and the focus lost end it too, as they end the buttons' toggles (host/nn_hid.c's reset).

   A press counts for Sprint only when it begins in gameplay: Link updated within GAMEPLAY_POLLS reads and no event (a
   conversation, a cutscene) running. Link's update goes on during a conversation, where the left stick's press hides the
   text box; Sprint has that press when its row is on (gpu/vk/menu/sprint.cpp moves it from LS), so a press of it that
   begins outside gameplay is handed to the game as LS for as long as it is held, and starts no sprint.

   Link's state: the id at [[[[Link +0x18] +0x270] +0x188] +0x1c0] +0x28 (+0x4c when that reads 0x11a, a state that wraps
   another), the same field the movement update compares with 1 (Walk) at 0x804ced50 before the game starts a sprint. */
#include "hos.h"
#include <stdint.h>
#include <string.h>

int hos_mem_region(uint64_t addr, uint64_t size);
extern volatile uint32_t hos_frame_seconds_bits;   /* host/nn_vi.c: the frame's seconds above a cap of 60, else 0 */

#define BIT_LS (1ull << 4)   /* (host/nn_hid.c's B_LS and B_ZL) */
#define BIT_ZL (1ull << 8)

#define DASH_INPUT 0x10u
#define LINK_CLASS 0x84b6dfc0ull      /* Link's actor class */
#define ACTOR_MGR 0x84c9e0d0ull       /* the pointer to the actor manager pointer */
#define ACTOR_MGR_CLASS 0x84bffe68ull
#define CONTROLLED_CLASS 0x84b69698ull
#define EVENT_MGR 0x84c9e090ull       /* the pointer to the event manager pointer */
#define GAMEPLAY_POLLS 8              /* a press is Sprint's while Link's update ran within this many reads */
/* Standing still this long ends a toggled sprint that has moved, or this long after a press while standing, which has not
   moved yet. In 60 Hz frames: above 60 fps Link updates once a rendered frame, each step worth the frame's time, so counting
   updates would end the sprint on any short stop at 240 fps (four updates are 17 ms there). */
#define WAIT_STOP_FRAMES 4.0
#define WAIT_START_FRAMES 90.0

/* What Link's state does to a sprint: MOVE and KEEP keep it, WAIT ends it after a few frames, STOP ends it at once. A state
   not listed stops it too. */
enum { MOVE, KEEP, WAIT, STOP };
static const struct { int32_t id; uint8_t kind; } states[] = {
    {1, MOVE},     /* Walk (also running) */
    {13, MOVE},    /* Jump */
    {32, MOVE},    /* Land */
    {33, MOVE},    /* Fall */
    {68, MOVE},    /* Clamber */
    {253, KEEP},   /* StepOn */
    {35, KEEP},    /* Slide */
    {36, KEEP},    /* SlideLand */
    {99, KEEP},    /* Rolling: a roll keeps a toggled sprint, which goes on when it ends */
    {40, WAIT},    /* Wait */
};

/* hos_sprint_input (the controllers' reads) -> hos_sprint_step (Link's update) */
static volatile int enabled;        /* the Sprint row on */
static volatile int toggled;        /* the Toggle on */
static volatile int pressed;        /* a press of Sprint's since Link's last update */
static volatile int reset_pending;  /* a toggled sprint ends at Link's next update */
static volatile int held;           /* Sprint held at the last read */
static volatile int latched;        /* the press now held began in gameplay: Sprint's */
static volatile int zl_held;        /* ZL (targeting) held at the last read */
static volatile int polls = GAMEPLAY_POLLS;   /* reads since Link's last update (none yet: not gameplay) */
static volatile int active;         /* the sprint is on: the dash input goes in */
static int moved;                   /* hos_sprint_step only: the sprint has moved since it started */
static double wait_frames;          /* hos_sprint_step only: 60 Hz frames stood still */

#define LOAD(p) __atomic_load_n(&(p), __ATOMIC_RELAXED)
#define STORE(p, v) __atomic_store_n(&(p), (v), __ATOMIC_RELAXED)

/* 60 Hz frames one update of Link is worth: the frame's time above a cap of 60, else 1 */
static double frame_step_frames(void) {
    const uint32_t bits = __atomic_load_n(&hos_frame_seconds_bits, __ATOMIC_RELAXED);
    if (!bits) return 1.0;
    float secs;
    memcpy(&secs, &bits, 4);
    return secs > 0.0f ? secs * 60.0 : 1.0;
}

/* Guest reads through Link's pointers: a stale pointer in the chain reads 0 instead of faulting. */
static uint64_t rd64(uint64_t a) { return a && hos_mem_region(a, 8) ? G64(a) : 0; }
static int32_t rd32(uint64_t a) { return a && hos_mem_region(a, 4) ? (int32_t)G32(a) : 0; }

/* 1 when link is the Link the player controls. Link's class has one more instance the player does not control: the game's
   actor manager names the controlled one at +0x470, whose +0x18 must be Link's own +0x18. */
static int is_link(uint64_t link) {
    if (!link || rd64(link) != g_module_base + LINK_CLASS) return 0;
    const uint64_t mgr = rd64(rd64(g_module_base + ACTOR_MGR));
    if (!mgr || rd64(mgr) != g_module_base + ACTOR_MGR_CLASS) return 0;
    const uint64_t ctl = rd64(mgr + 0x470);
    if (!ctl || rd64(ctl) != g_module_base + CONTROLLED_CLASS) return 0;
    const uint64_t actor = rd64(ctl + 0x18);
    return actor && actor == rd64(link + 0x18);
}

/* An event (a conversation, a cutscene) is running: the event manager has an active event at +0x68, the field the game's
   SeqQueryIsEventRunning reads. */
static int event_running(void) {
    const uint64_t m = rd64(rd64(g_module_base + EVENT_MGR));
    return m && rd64(m + 0x68);
}

static int state_kind(uint64_t link) {
    uint64_t p = rd64(link + 0x18);
    if (p) p = rd64(p + 0x270);
    if (p) p = rd64(p + 0x188);
    if (p) p = rd64(p + 0x1c0);
    if (!p) return STOP;
    int32_t id = rd32(p + 0x28);
    if (id == 0x11a) id = rd32(p + 0x4c);
    for (size_t i = 0; i < sizeof states / sizeof *states; i++)
        if (states[i].id == id) return states[i].kind;
    return STOP;
}

/* host/nn_hid.c npad_states, the game's thread: each read of the controllers. buttons: what the game gets; on: the Sprint
   row; down: Sprint's key, gamepad input or a test run's SPRINT held; l3: that gamepad input is the left stick's press;
   toggle: Sprint's Toggle; reset: a toggled sprint ends (Insert opened, Plus or Minus, the focus lost); blocked: no press
   is Sprint's or the game's now (Insert open, the ocarina waiting for a note, a test run that keeps the player out).
   Returns the buttons, with LS while a press of the left stick that began outside gameplay is held. */
uint64_t hos_sprint_input(uint64_t buttons, int on, int down, int l3, int toggle, int reset, int blocked) {
    static int was_down, passing;
    down = down != 0;
    if (!on || toggle != LOAD(toggled)) STORE(active, 0), STORE(pressed, 0);
    STORE(enabled, on), STORE(toggled, toggle);
    if (!on) {
        STORE(latched, 0), STORE(held, 0);
        STORE(polls, GAMEPLAY_POLLS);
        was_down = down, passing = 0;
        return buttons;
    }
    const int n = LOAD(polls);
    if (n <= GAMEPLAY_POLLS) STORE(polls, n + 1);
    const int event = event_running();
    const int gameplay = n < GAMEPLAY_POLLS && !event;
    /* A press is decided at its start, for its whole length: Sprint's when it begins in gameplay; else the game's (the
       left stick's press as LS), and no sprint's. */
    if (down && !was_down) {
        const int mine = gameplay && !blocked;
        STORE(latched, mine);
        if (mine) STORE(pressed, 1);
        passing = !mine && !blocked && l3;
    } else if (!down) {
        STORE(latched, 0);
        passing = 0;
    }
    if (blocked || event) {
        STORE(latched, 0), STORE(active, 0), STORE(pressed, 0);
        if (blocked) passing = 0;
    }
    was_down = down;
    if (reset) STORE(reset_pending, 1), STORE(pressed, 0);
    STORE(held, down);
    STORE(zl_held, (buttons & BIT_ZL) != 0);
    return passing ? buttons | BIT_LS : buttons;
}

/* The sprint for this update of Link, from his state and Sprint's presses: 1 when it is on. */
int hos_sprint_step(int kind) {
    const int press = __atomic_exchange_n(&pressed, 0, __ATOMIC_RELAXED);
    int on = LOAD(active);
    if (__atomic_exchange_n(&reset_pending, 0, __ATOMIC_RELAXED)) on = 0;
    if (LOAD(zl_held)) kind = STOP;
    if (!LOAD(enabled)) on = 0;
    else if (!LOAD(toggled)) on = LOAD(held) && LOAD(latched) && kind != STOP;
    else if (press) {
        if (on) on = 0;
        else if (kind != STOP) on = 1, moved = kind == MOVE, wait_frames = 0;
    } else if (on) {
        if (kind == STOP) on = 0;
        else if (kind == WAIT) {
            wait_frames += frame_step_frames();
            if (wait_frames > (moved ? WAIT_STOP_FRAMES : WAIT_START_FRAMES)) on = 0;
        } else {
            wait_frames = 0;
            moved |= kind == MOVE;
        }
    }
    STORE(active, on);
    return on;
}

/* (nx2/oot_patches.py) f_804caa80 at 0x804ce798, after its load of Link's input flags into x10 (Link in x19): the flags
   for the sprint's start and keep tests. */
void hos_sprint_link(Ctx *c) {
    if (!LOAD(enabled)) return;
    if (!is_link(c->x[19])) return;
    STORE(polls, 0);
    if (hos_sprint_step(state_kind(c->x[19]))) c->x[10] |= DASH_INPUT;
}

/* (nx2/oot_patches.py) f_80c225d0 at 0x80c27f3c, after its load of the same flags' low byte into x8 (Link in x19): the
   stance and camera update. */
void hos_sprint_stance(Ctx *c) {
    if (LOAD(enabled) && LOAD(active) && is_link(c->x[19])) c->x[8] |= DASH_INPUT;
}
