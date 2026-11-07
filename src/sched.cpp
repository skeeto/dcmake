// Render scheduler: when the GUI draws a frame
//
// The GUI renders on demand, and an idle window draws nothing at all.
// Ported from Prospero (~/src/harness, src/sched.cpp and the scheduler's
// view of ImGui in src/ui.cpp).  Each GUI half's loop waits, then
// dispatches every pending event, then decides:
//
//   for (;;) {
//       wait up to schedtimeout()           // OS input, a wake, or a deadline
//       dispatch everything pending          // schedinput, scheddirty, schedforce
//       now = sched_ticks(); if (scheddue()) frame()
//   }
//
// Deciding only after the queue is empty is what kept dcmake's first
// attempt (46a44b23) one event behind: it drew after waking but before
// dispatching what woke it.  A frame (schedbegin to schedend) consumes
// the requests made before it began, so requests made during the frame,
// by the UI or by a wake dispatched from inside a modal loop, survive to
// the next one.
//
// ImGui needs several frames to settle after an event (windows appear a
// frame late, popups auto-fit over two), and drives changes from timers
// with no event at all (tooltips, the caret, the ini save).  So input owes
// at least SETTLE_INPUT frames and a wake SETTLE_WAKE, and frames continue
// until two in a row hash the same.  Meanwhile the UI declares its timers
// each frame with schedat, of which only the earliest is kept, and its
// reasons to keep going (held buttons, fades, dcmake's animations)
// through the busy mask.
//
// A held key is busy, as ImGui repeats it on its own timer, but only
// while the OS confirms it: a key truly held auto-repeats, each repeat
// another key event (sched_keyevent).  One with no event for KEY_STALE
// lost its release (focus taken mid-press), and the frame lets go of it
// rather than draw on forever.  ImGui drops the repeats from its own
// queue, so the platforms report key events themselves.
//
// Present is skipped when a frame hashes the same as what is on screen,
// but frames stay paced to the display refresh either way.
//
// The staleness probe (DCMAKE_SCHED_DEBUG=1) checks all of this:
// PROBE_DELAY after going idle it renders one frame nobody asked for.  If
// that frame differs from the screen, a wake was missed, and stderr gets
// the windows that changed.
//
// Nothing may make a frame differ from the last without declaring why:
// a background thread must call platform_wake, a timer must schedat its
// deadline, and an animation must report itself busy.

#include "sched.hpp"

#include <chrono>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>

enum : int {
    SETTLE_INPUT = 3,    // frames owed after OS input
    SETTLE_WAKE  = 2,    // frames owed when a wake may change the view
    RUNAWAY      = 600,  // changing frames in a row before giving up
};

enum : int64_t {
    PROBE_DELAY   = 250000,    // µs after going idle
    PROBE_SLACK   = 50000,     // µs: no probe this close before a deadline
    KEY_STALE     = 2500000,   // µs: a key held with no repeat was released
    CARET_STOP    = 10000000,  // µs after the last key: stop blinking
};

static const char *const busynames[] = {
    "mouse", "key", "dim", "navwin", "drag", "hidden", "queue", "scroll",
    "tabs", "dock", "app", "layout",
};
static_assert(sizeof(busynames)/sizeof(*busynames) == BUSY_COUNT);

int64_t sched_ticks()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

static int maxi(int a, int b)
{
    return a>b ? a : b;
}

Sched newsched(int64_t now, int64_t pace)
{
    Sched s = {};
    s.now       = now;
    s.pace      = pace;
    s.lastframe = now - pace;  // the first frame need not wait
    s.deadline  = INT64_MAX;
    s.owed      = SETTLE_INPUT;  // the first frames
    s.force     = true;
    const char *dbg = getenv("DCMAKE_SCHED_DEBUG");
    s.probing   = dbg && *dbg && strcmp(dbg, "0");
    s.trace     = dbg && !strcmp(dbg, "2");
    return s;
}

// OS input of any kind arrived (and went to ImGui).
void schedinput(Sched *s)
{
    s->owed     = maxi(s->owed, SETTLE_INPUT);
    s->shadowat = 0;
}

// A key event arrived at time t (µs): a press, a release, or the OS's
// auto-repeat of a key held.
void schedkey(Sched *s, int64_t t)
{
    s->keyat   = t;
    s->keylost = false;
}

// Something may have changed what the view shows: a wake.
void scheddirty(Sched *s)
{
    s->owed = maxi(s->owed, SETTLE_WAKE);
}

// The next frame must present: resized, exposed, rescaled, restored.
void schedforce(Sched *s)
{
    s->force = true;
    s->owed  = maxi(s->owed, SETTLE_WAKE);
}

// Ask for a frame at time t (µs).  Only the earliest request survives,
// until a frame begins and consumes it.
void schedat(Sched *s, int64_t t, int why)
{
    if (t < s->deadline) {
        s->deadline = t;
        s->duewhy   = why;
    }
}

void schedminimize(Sched *s, bool minimized)
{
    if (s->minimized && !minimized) {
        schedforce(s);
    }
    s->minimized = minimized;
}

// How long the loop may wait, in µs, or -1 to wait for an event.
int64_t schedtimeout(Sched *s)
{
    if (s->minimized) {
        return -1;  // still woken by platform_wake
    }
    if (s->owed || s->busy || s->force) {
        int64_t t = s->lastframe + s->pace - s->now;
        return t>0 ? t : 0;
    }
    int64_t t = s->deadline;
    if (s->shadowat && s->shadowat<t) {
        t = s->shadowat;
    }
    if (t == INT64_MAX) {
        return -1;
    }
    t -= s->now;
    return t>0 ? t : 0;
}

// Whether to draw a frame now.  Expired deadlines and probes become owed
// frames here, so pacing may still defer them to a later call.  A probe
// is a frame nothing asked for: one that coincides with a deadline or
// new work is an ordinary frame, whose changes were declared.  Nor does
// a probe run just before a deadline, which by design lands a little
// after the change it waits for: the probe would see the change early.
bool scheddue(Sched *s)
{
    if (s->deadline <= s->now) {
        s->deadline = INT64_MAX;
        s->owed     = maxi(s->owed, 1);
    }
    if (s->shadowat && s->shadowat<=s->now) {
        s->shadowat = 0;
        bool quiet = !s->owed && !s->busy && !s->force;
        if (quiet && s->deadline-s->now>PROBE_SLACK) {
            s->probe = true;
            s->owed  = 1;
        }
    }
    return !s->minimized && (s->owed || s->busy || s->force) &&
           s->now >= s->lastframe + s->pace;
}

// Begin a frame, consuming the requests made so far.  Returns false if a
// frame is already running, which then owes another.
bool schedbegin(Sched *s)
{
    if (s->inframe) {
        s->owed = maxi(s->owed, 1);
        return false;
    }
    s->inframe   = true;
    s->lastframe = s->now;
    s->deadline  = INT64_MAX;
    s->duewhy    = DUE_NONE;
    return true;
}

// End a frame given its draw hash and the UI's busy reasons.
Frame schedend(Sched *s, uint64_t hash, uint32_t busy)
{
    Frame r = {};
    if (busy&BUSY_KEY && s->now-s->keyat>=KEY_STALE) {
        // Its release was lost: the caller lets go of it, once, and it
        // is busy no more whether or not that worked
        busy      &= ~(uint32_t)BUSY_KEY;
        r.keyup    = !s->keylost;
        s->keylost = true;
    }
    r.probe   = s->probe;
    r.present = s->force || hash!=s->shownhash;
    r.stale   = s->probe && r.present && !s->force;
    if (r.present) {
        s->shownhash = hash;
    }

    s->busy     = busy;
    s->changing = hash!=s->prevhash ? s->changing+1 : 0;
    s->prevhash = hash;
    s->owed     = maxi(s->owed-1, 0);
    if (s->changing) {
        s->owed = maxi(s->owed, 1);  // until two frames match
    }
    if (r.keyup) {
        s->owed = maxi(s->owed, 1);  // to take in the release
    }
    if (s->changing>=RUNAWAY && !busy) {
        // An animation nobody declared: stop rather than spin forever.
        s->owed     = 0;
        s->changing = 0;
        r.runaway   = true;
    }

    s->force = false;
    s->probe = false;
    if (!s->owed && !s->busy && s->probing && !r.probe) {
        s->shadowat = s->now + PROBE_DELAY;  // one probe per sleep
    } else {
        s->shadowat = 0;
    }
    s->inframe = false;
    return r;
}


// Draw-data hashing: 64-bit multiply-xorshift over 8-byte words.

struct Hash64 {
    uint64_t h;
};

static void hashword(Hash64 *h, uint64_t w)
{
    uint64_t x = h->h ^ w;
    x *= 0x9e3779b97f4a7c15u;
    x ^= x >> 31;
    h->h = x;
}

static void hashbytes(Hash64 *h, const void *p, size_t len)
{
    const unsigned char *s = (const unsigned char *)p;
    size_t i = 0;
    for (; i+8 <= len; i += 8) {
        uint64_t w;
        memcpy(&w, s+i, 8);
        hashword(h, w);
    }
    uint64_t w = (uint64_t)len << 56;
    for (int k = 0; i < len; i++, k++) {
        w |= (uint64_t)s[i] << (8*k);
    }
    hashword(h, w);
}

static uint64_t hashfinal(Hash64 *h)
{
    uint64_t x = h->h;
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93u;
    x ^= x >> 32;
    return x;
}

// Hash everything that reaches the screen: sizes, vertices, indices,
// and each command's clip rectangle, texture and ranges.  Per-list
// hashes, by owner window, explain a stale frame.
static uint64_t drawhash(ImDrawData *dd, Sched *s)
{
    Hash64 h = {};
    float hdr[] = {
        dd->DisplayPos.x, dd->DisplayPos.y, dd->DisplaySize.x, dd->DisplaySize.y,
        dd->FramebufferScale.x, dd->FramebufferScale.y,
    };
    hashbytes(&h, hdr, sizeof(hdr));
    s->layoutlen = 0;
    for (int i = 0; i < dd->CmdLists.Size; i++) {
        ImDrawList *dl = dd->CmdLists[i];
        Hash64 lh = {};
        hashbytes(&lh, dl->VtxBuffer.Data, (size_t)dl->VtxBuffer.Size*sizeof(ImDrawVert));
        hashbytes(&lh, dl->IdxBuffer.Data, (size_t)dl->IdxBuffer.Size*sizeof(ImDrawIdx));
        for (int c = 0; c < dl->CmdBuffer.Size; c++) {
            ImDrawCmd *cmd = &dl->CmdBuffer[c];
            hashbytes(&lh, &cmd->ClipRect, sizeof(cmd->ClipRect));
            hashword(&lh, (uint64_t)(uintptr_t)cmd->TexRef._TexData);
            hashword(&lh, (uint64_t)cmd->TexRef._TexID);
            hashword(&lh, (uint64_t)cmd->ElemCount<<32 | cmd->VtxOffset);
            hashword(&lh, (uint64_t)cmd->IdxOffset);
            hashword(&lh, (uint64_t)(uintptr_t)cmd->UserCallback);
        }
        uint64_t lv = hashfinal(&lh);
        hashword(&h, lv);
        if (s->probing && s->layoutlen < SCHED_LISTS) {
            ListHash *l = &s->layout[s->layoutlen++];
            snprintf(l->owner, sizeof(l->owner), "%s",
                     dl->_OwnerName ? dl->_OwnerName : "?");
            l->hash = lv;
        }
    }
    return hashfinal(&h);
}


// The scheduler's view of ImGui

static bool modifierkey(int k)
{
    return (k>=ImGuiKey_LeftCtrl && k<=ImGuiKey_RightSuper) ||
           (k>=ImGuiKey_ReservedForModCtrl && k<=ImGuiKey_ReservedForModSuper);
}

// A window's content size as the next frame will compute it from this
// one's layout, as ImGui's CalcWindowContentSizes does.
static bool contentmoves(ImGuiWindow *w)
{
    if (w->Collapsed || w->Hidden) {
        return false;
    }
    float x = w->ContentSizeExplicit.x!=0 ? w->ContentSizeExplicit.x
            : (float)(int64_t)(w->DC.CursorMaxPos.x - w->DC.CursorStartPos.x);
    float y = w->ContentSizeExplicit.y!=0 ? w->ContentSizeExplicit.y
            : (float)(int64_t)(w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y);
    return x!=w->ContentSize.x || y!=w->ContentSize.y;
}

// Reasons the frame just rendered needs a successor.
static uint32_t uiscan()
{
    ImGuiContext &c  = *GImGui;
    ImGuiIO      &io = c.IO;
    uint32_t busy = 0;
    for (int i = 0; i < 5; i++) {
        busy |= io.MouseDown[i] ? (uint32_t)BUSY_MOUSE : 0;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        if (k>=ImGuiKey_MouseLeft && k<=ImGuiKey_MouseWheelY) continue;
        if (!modifierkey(k) && ImGui::IsKeyDown((ImGuiKey)k)) {
            busy |= BUSY_KEY;
            break;
        }
    }
    if (c.DimBgRatio>0 && c.DimBgRatio<1) busy |= BUSY_DIM;
    if (c.NavWindowingTarget) busy |= BUSY_NAVWIN;
    if (c.NavWindowingTargetAnim && c.NavWindowingHighlightAlpha>0) busy |= BUSY_NAVWIN;
    if (c.DragDropActive) busy |= BUSY_DRAG;
    if (c.InputEventsQueue.Size) busy |= BUSY_QUEUE;
    if (c.DockContext.Requests.Size) busy |= BUSY_DOCK;
    for (int i = 0; i < c.Windows.Size; i++) {
        ImGuiWindow *w = c.Windows[i];
        if (!w->Active && !w->WasActive) continue;
        // A window measuring itself before it appears (new windows,
        // reopened popups and tooltips, a tab about to be selected) is
        // drawn next frame.  Windows hidden for good (other dock tabs,
        // clipped children) and auto-fitting ones are not pending:
        // the latter change the hash as they go.
        if (w->HiddenFramesCannotSkipItems>0 || w->HiddenFramesForRenderOnly>0) {
            busy |= BUSY_HIDDEN;
        }
        if (w->ScrollTarget.x<FLT_MAX || w->ScrollTarget.y<FLT_MAX) {
            busy |= BUSY_SCROLL;
        }
        // Content grown or shrunk this frame shows in the scrollbar only
        // next frame, even when nothing visible moved now: a line added
        // below a scrolled window's bottom edge.
        if (w->Active && contentmoves(w)) {
            busy |= BUSY_LAYOUT;
        }
    }
    for (int i = 0; i < c.TabBars.GetMapSize(); i++) {
        ImGuiTabBar *tb = c.TabBars.TryGetMapData(i);
        if (tb && (tb->ScrollingAnim!=tb->ScrollingTarget || tb->ReorderRequestTabId)) {
            busy |= BUSY_TABS;
        }
    }
    return busy;
}

// Let go of every key ImGui holds but modifiers, as busy counts them:
// their releases were lost (schedend's keyup).  Returns how many.
static int uikeysup()
{
    ImGuiIO &io = ImGui::GetIO();
    int      n  = 0;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        if (k>=ImGuiKey_MouseLeft && k<=ImGuiKey_MouseWheelY) continue;
        if (!modifierkey(k) && ImGui::IsKeyDown((ImGuiKey)k)) {
            io.AddKeyEvent((ImGuiKey)k, false);
            n++;
        }
    }
    return n;
}

// Timers ImGui runs without any event: tooltips, the caret, the
// settings save.  Declares a deadline for each pending one.
static void uideadlines(Sched *s)
{
    ImGuiContext &c  = *GImGui;
    ImGuiIO      &io = c.IO;
    ImGuiStyle   &st = c.Style;
    int64_t now = s->now;

    // Hover delays restart whenever the mouse moves, or whenever what is
    // under it changes without moving it (a resize, a scroll): splitters
    // light up after WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER, 0.04 s.
    if (io.MouseDelta.x!=0 || io.MouseDelta.y!=0 || c.HoveredId!=s->lasthover) {
        s->lastmove  = now;
        s->lasthover = c.HoveredId;
    }
    if (s->lastmove) {
        int64_t slack = 20000;
        float delays[] = {
            0.04f,
            st.HoverStationaryDelay,
            st.HoverStationaryDelay + st.HoverDelayShort,
            st.HoverStationaryDelay + st.HoverDelayNormal,
        };
        for (int i = 0; i < (int)(sizeof(delays)/sizeof(*delays)); i++) {
            int64_t t = s->lastmove + (int64_t)(delays[i]*1e6f) + slack;
            if (t > now) {
                schedat(s, t, i>1 ? DUE_TOOLTIP : DUE_HOVER);
            }
        }
    }

    // The caret blinks: on for 0.8 s, off for 0.4 s, after 0.3 s held on
    // following each edit.  Blinking stops CARET_STOP after the last key.
    if (c.ActiveId && c.InputTextState.ID==c.ActiveId && io.ConfigInputTextCursorBlink) {
        float a  = c.InputTextState.CursorAnim;
        float dt = 0;
        if (a < 0) {
            dt = 0.8f - a;
        } else {
            float r = ImFmod(a, 1.2f);
            dt = r<0.8f ? 0.8f-r : 1.2f-r;
        }
        schedat(s, now + (int64_t)(dt*1e6f) + 5000, DUE_CARET);
        schedat(s, s->lastkey + CARET_STOP, DUE_CARET);
    }

    if (c.SettingsDirtyTimer > 0) {
        schedat(s, now + (int64_t)(c.SettingsDirtyTimer*1e6f) + 1000, DUE_SETTINGS);
    }
}


// Glue for the platform loops

int sched_queued()
{
    return GImGui->InputEventsQueue.Size;
}

// Typing or clicking into a field shows a blinking caret again.
static void caretwake(Sched *s)
{
    s->lastkey = sched_ticks();
    ImGui::GetIO().ConfigInputTextCursorBlink = true;
}

void sched_takeinput(Sched *s, int before)
{
    ImVector<ImGuiInputEvent> &q = GImGui->InputEventsQueue;
    if (q.Size <= before) {
        return;
    }
    schedinput(s);
    for (int i = before; i < q.Size; i++) {
        ImGuiInputEventType t = q[i].Type;
        if (t==ImGuiInputEventType_Key || t==ImGuiInputEventType_Text ||
                t==ImGuiInputEventType_MouseButton) {
            caretwake(s);
            break;
        }
    }
}

void sched_keyevent(Sched *s)
{
    schedinput(s);
    schedkey(s, sched_ticks());
    caretwake(s);
}

bool sched_frame_start(Sched *s)
{
    s->now = sched_ticks();
    if (!schedbegin(s)) {
        return false;
    }
    if (s->now-s->lastkey >= CARET_STOP) {
        ImGui::GetIO().ConfigInputTextCursorBlink = false;
    }
    return true;
}

// Which windows' draw lists differ from the screen, for a stale frame.
static void stalereport(Sched *s)
{
    fprintf(stderr, "dcmake: stale frame (a missed wake), changed:");
    for (int i = 0; i < s->layoutlen; i++) {
        ListHash *l = &s->layout[i];
        bool same = false;
        for (int j = 0; j < s->shownlen; j++) {
            if (!strcmp(s->shown[j].owner, l->owner)) {
                same = s->shown[j].hash == l->hash;
                break;
            }
        }
        if (!same) {
            fprintf(stderr, " \"%s\"", l->owner);
        }
    }
    fprintf(stderr, "\n");
}

Frame sched_frame_end(Sched *s, uint32_t busy)
{
    busy |= uiscan();
    // A window whose content size never settles would keep this busy
    // forever: after a few frames, leave it to the hash.
    s->layoutstreak = busy&BUSY_LAYOUT ? s->layoutstreak+1 : 0;
    if (s->layoutstreak > 4) {
        busy &= ~(uint32_t)BUSY_LAYOUT;
    }
    uideadlines(s);
    uint64_t hash = drawhash(ImGui::GetDrawData(), s);
    int duewhy = s->duewhy;
    int64_t deadline = s->deadline;
    Frame f = schedend(s, hash, busy);
    if (s->trace) {
        static const char *const duenames[] = {
            "none", "tooltip", "caret", "settings", "hover",
        };
        fprintf(stderr, "frame %lld%s%s, owed %d, changing %d",
                (long long)(s->now/1000), f.present ? ", present" : "",
                f.stale ? ", STALE" : "", s->owed, s->changing);
        if (deadline != INT64_MAX)
            fprintf(stderr, ", next %s in %lld ms", duenames[duewhy],
                    (long long)((deadline - s->now)/1000));
        for (int i = 0; i < BUSY_COUNT; i++)
            if (busy & (1u<<i)) fprintf(stderr, ", %s", busynames[i]);
        fprintf(stderr, "\n");
    }
    if (f.keyup) {
        int n = uikeysup();
        if (s->probing) {
            fprintf(stderr, "dcmake: no key event for %lld ms with a key "
                    "held: its release was lost, let go of %d\n",
                    (long long)((s->now - s->keyat)/1000), n);
        }
    }
    if (s->probing) {
        if (f.stale) {
            stalereport(s);
        }
        if (f.runaway) {
            fprintf(stderr, "dcmake: undeclared animation: frames kept "
                    "changing with no reason given\n");
        }
        if (f.present) {
            memcpy(s->shown, s->layout, sizeof(ListHash)*(size_t)s->layoutlen);
            s->shownlen = s->layoutlen;
        }
        if (busy && s->changing > 120 && s->changing%120 == 0) {
            fprintf(stderr, "dcmake: %d changing frames, busy:", s->changing);
            for (int i = 0; i < BUSY_COUNT; i++) {
                if (busy & (1u<<i)) fprintf(stderr, " %s", busynames[i]);
            }
            fprintf(stderr, "\n");
        }
    }
    return f;
}
