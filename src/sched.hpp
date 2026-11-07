#pragma once

#include <cstdint>

// Render scheduler: when the GUI draws a frame.  Ported from Prospero
// (~/src/harness, src/sched.cpp); see sched.cpp for the design.

// Reasons a frame needs a successor (sched_frame_end's busy mask)
enum : uint32_t {
    BUSY_MOUSE  = 1u<<0,   // a mouse button is held
    BUSY_KEY    = 1u<<1,   // a non-modifier key is held: key repeat
    BUSY_DIM    = 1u<<2,   // a modal's dimming fades in or out
    BUSY_NAVWIN = 1u<<3,   // the Ctrl+Tab window switcher is up
    BUSY_DRAG   = 1u<<4,   // drag and drop
    BUSY_HIDDEN = 1u<<5,   // a window is hidden while it measures itself
    BUSY_QUEUE  = 1u<<6,   // trickled input events are still queued
    BUSY_SCROLL = 1u<<7,   // a scroll request lands next frame
    BUSY_TABS   = 1u<<8,   // a tab bar scrolls or reorders
    BUSY_DOCK   = 1u<<9,   // docking requests are pending
    BUSY_APP    = 1u<<10,  // dcmake animates (smooth scroll, line flash)
    BUSY_LAYOUT = 1u<<11,  // a window's content size changes next frame
};

enum { BUSY_COUNT = 12 };

// Deadline sources, recorded with the earliest deadline
enum {
    DUE_NONE,
    DUE_TOOLTIP,
    DUE_CARET,
    DUE_SETTINGS,
    DUE_HOVER,
    DUE_COUNT,
};

struct ListHash {
    char     owner[48];
    uint64_t hash;
};

enum { SCHED_LISTS = 64 };

struct Sched {
    int64_t  now;        // µs, set by the loop before scheddue
    int64_t  pace;       // minimum frame interval: the refresh period
    int64_t  lastframe;  // start of the last frame
    int64_t  deadline;   // earliest requested wake, INT64_MAX if none
    int      duewhy;     // DUE_* source of deadline
    int      owed;       // settle frames owed
    int      changing;   // frames in a row whose hash differed from the last
    uint32_t busy;       // BUSY_* reasons from the last frame
    bool     force;      // must present: resize, expose, theme change
    bool     inframe;    // a frame is running
    bool     minimized;  // nothing to draw on: wait for restore or a wake
    bool     probing;    // staleness probe enabled (DCMAKE_SCHED_DEBUG)
    bool     trace;      // a line per frame to stderr (DCMAKE_SCHED_DEBUG=2)
    bool     probe;      // the running frame is a probe
    bool     keylost;    // a key went stale since keyat (keyup given)
    int64_t  shadowat;   // when the next probe runs, 0 if none
    int64_t  keyat;      // the last key event: down, up, or an auto-repeat
    uint64_t prevhash;   // last frame's draw hash
    uint64_t shownhash;  // the presented draw hash

    // The scheduler's view of ImGui
    int64_t  lastmove;   // the mouse last moved, or what is under it changed
    unsigned lasthover;  // ImGuiID hovered then
    int64_t  lastkey;    // last key, text or button input: the caret blinks
    int      layoutstreak;
    int      layoutlen;  // per-list hashes of the last frame, and shown
    int      shownlen;
    ListHash layout[SCHED_LISTS];
    ListHash shown[SCHED_LISTS];
};

// The outcome of a frame, from schedend.
struct Frame {
    bool present;  // render and present it, else only upload textures
    bool stale;    // a probe found the screen out of date: a missed wake
    bool runaway;  // it kept changing with no declared reason, so stopped
    bool probe;    // it was a probe
    bool keyup;    // a key held with no repeat for KEY_STALE: release it
};

int64_t sched_ticks();  // monotonic µs
Sched   newsched(int64_t now, int64_t pace);
void    schedinput(Sched *s);
void    schedkey(Sched *s, int64_t t);
void    scheddirty(Sched *s);
void    schedforce(Sched *s);
void    schedat(Sched *s, int64_t t, int why);
void    schedminimize(Sched *s, bool minimized);
int64_t schedtimeout(Sched *s);
bool    scheddue(Sched *s);
bool    schedbegin(Sched *s);
Frame   schedend(Sched *s, uint64_t hash, uint32_t busy);

// Glue between the platform loops and ImGui, shared by both GUI halves.
int  sched_queued();                       // ImGui input events queued now
void sched_takeinput(Sched *s, int before); // input since sched_queued()
void sched_keyevent(Sched *s);             // a key event, auto-repeats too
bool sched_frame_start(Sched *s);          // begin a frame, false if one runs
Frame sched_frame_end(Sched *s, uint32_t busy);  // after ImGui::Render
