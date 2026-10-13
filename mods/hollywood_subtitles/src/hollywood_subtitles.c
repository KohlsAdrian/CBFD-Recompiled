// Hollywood Subtitles: movie-style subtitles for Conker's Bad Fur Day's voice acting.
//
// All the logic lives here, in the mod. The host only draws the text, through the subtitle overlay it
// exports (recomp_subtitle_show / recomp_subtitle_hide, see mods/include/recompsubtitles.h). The game's
// own code is untouched: the mod only watches the game's MP3 voice player.
//
// Timing comes from the game's own voice playback (the MP3 player the decomp names, from Rare's library):
//   - mp3_play_file(romaddr, filesize) (func_151F2960) starts each voice clip, with its ROM address. A clip
//     is one recording: one line, or a long speech (Conker's throne-room monologue is one 38.8 s clip).
//   - mp3main_continue_file (func_151F86B0) decodes the playing clip's next MPEG-2 Layer III frame: 576
//     samples at 22050 Hz, 26.1 ms. Counting the frames since the clip started gives its position exactly,
//     and it stops while the game is paused (the player only decodes while it's playing).
//   - The player's state (0x800E0E04): 1 playing, 2 paused, 5 loading, 7 unpausing; 0 or 3 once stopped.
// So the transcript gives each clip's lines as times from the clip's start, and the mod shows whichever
// line holds the clip's position now. Measured: the throne monologue decodes 1484 frames in every run,
// with two pauses or none, and its speech segments land where they were measured from the decoded audio.
// (The first version counted in the cutscene timer instead, 2 ticks a game frame: it falls behind the
// voice whenever the game draws fewer than 30 frames a second, which is why its lines drifted later on.)
//
// The mod hooks no function conker.toml hooks (a mod's hook replaces the toml's): so not func_15095D34, the
// speech bubbles' pieces, which the host hooks for widescreen and a crash. Hide Speech Bubbles waits on a way
// to tell speech bubbles from Conker's thought bubbles that doesn't hook it.
//
// The two player hooks run on the game's audio thread, which the runtime schedules with the game's other
// threads: they only store numbers (no printing, no options, no host calls). Printing there (in a test
// version on the host's side) hung the game. Everything else runs once a game frame, from the hook on
// func_1501BBB8.

#include "modding.h"
#include "PR/ultratypes.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "recompsubtitles.h"

// --- The voice player (read-only) -----------------------------------------------------------------

// The player's state (g_Mp3Vars.state).
#define g_mp3_state (*(volatile u32*)0x800E0E04)
#define MP3_IDLE 0
#define MP3_STOPPED 3

// Set by the hooks below (audio thread), read each game frame.
static volatile u32 clip_rom = 0;     // the clip's ROM address, as the game passes it to mp3_play_file
static volatile u32 clip_frames = 0;  // MP3 frames decoded since it started
static volatile u32 clip_serial = 0;  // clips started so far (tells a clip played twice apart)

RECOMP_HOOK("func_151F2960") void hollywood_on_clip_start(u32 romaddr, u32 filesize) {
    clip_rom = romaddr;
    clip_frames = 0;
    clip_serial++;
}

RECOMP_HOOK("func_151F86B0") void hollywood_on_clip_frame(void) {
    clip_frames++;
}

// Milliseconds into the clip: 576 samples a frame at 22050 Hz, 576000 / 22050 = 1280 / 49 ms a frame (in 32 bits:
// a mod has no 64-bit division, and 1280 x frames fits for over 900 hours).
static u32 clip_ms(void) {
    return (clip_frames * 1280u) / 49u;
}

// --- The transcript -------------------------------------------------------------------------------
//
// Each clip the game plays, by ROM address, with its lines as times (ms) from the clip's start. A line
// ending at WHOLE_CLIP stays up as long as the clip plays (a clip that is one line). is_intro marks the
// clips the Intro Cutscene Subtitles toggle controls; the rest go with General Game Subtitles.

#define WHOLE_CLIP 0xFFFFFFFFu
// Keep a line up this long after its time ends (ms), so it doesn't flash off between words.
#define LINE_TAIL_MS 300
// And a whole-clip line this many game frames after its clip stops (about 0.3 s).
#define CLIP_TAIL_FRAMES 10

typedef struct {
    u32 start_ms;
    u32 end_ms;
    const char* text; // 0: nothing shows (a clip that isn't speech)
} SubLine;

typedef struct {
    u32 rom;
    int is_intro;
    const SubLine* lines;
    int count;
} SubClip;

// The chainsaw boot (scene 0x21): seven clips, in play order. The first three are the "N" and the chainsaw.
static const SubLine chainsaw_1[] = { { 0, WHOLE_CLIP, 0 } };
static const SubLine chainsaw_2[] = { { 0, WHOLE_CLIP, 0 } };
static const SubLine chainsaw_3[] = { { 0, WHOLE_CLIP, 0 } };
static const SubLine chainsaw_4[] = { { 0, WHOLE_CLIP, "Stupid logo!" } };
static const SubLine chainsaw_5[] = { { 0, WHOLE_CLIP, "There, much better." } };
static const SubLine chainsaw_6[] = { { 0, WHOLE_CLIP, "How can... get it... Ah!" } };
static const SubLine chainsaw_7[] = { { 0, WHOLE_CLIP, "Marvelous." } };

// Conker's throne-room monologue (scene 0x18): one 38.8 s clip. The times are its speech segments,
// measured from the clip's decoded audio (its loudness each MP3 frame, in the game); they match the ones
// the first version measured offline to within 0.05 s. The script's last lines ("It all started...
// yesterday, and what a day that was! It's what I like to call... a bad fur day.") aren't in this clip,
// which ends at 38.8 s: they're in another one, still to be found and added.
static const SubLine throne[] = {
    {     50,   550, "Well." },
    {   1520,  2400, "There I am." },
    {   3580,  5150, "Conker the King." },
    {   6030,  8050, "King of all the land." },
    {   8880, 12800, "Who'd a thought that?" },
    {  13900, 17600, "But how did I come to this," },
    {  18130, 19100, "I hear you say." },
    {  20530, 29400, "And who are those strange fellows that surround my throne," },
    {  29990, 31250, "I hear you also say." },
    {  32080, 33400, "It's a long story." },
    {  34040, 36450, "Come closer, and I'll tell you." },
};

#define LINES(a) a, (int)(sizeof(a) / sizeof(a[0]))
static const SubClip clips[] = {
    { 0x017B3888, 1, LINES(chainsaw_1) },
    { 0x017AFB10, 1, LINES(chainsaw_2) },
    { 0x017B1CA0, 1, LINES(chainsaw_3) },
    { 0x017AA440, 1, LINES(chainsaw_4) },
    { 0x017A8EC8, 1, LINES(chainsaw_5) },
    { 0x017AE1D8, 1, LINES(chainsaw_6) },
    { 0x017A99E0, 1, LINES(chainsaw_7) },
    { 0x015E1FD8, 1, LINES(throne) },
};
#define CLIP_COUNT ((int)(sizeof(clips) / sizeof(clips[0])))

static const SubClip* find_clip(u32 rom) {
    for (int i = 0; i < CLIP_COUNT; i++) {
        if (clips[i].rom == rom) {
            return &clips[i];
        }
    }
    return 0;
}

// The text that holds this time in the clip (with its tail), or 0.
static const char* text_at(const SubClip* clip, u32 ms) {
    for (int i = 0; i < clip->count; i++) {
        const SubLine* line = &clip->lines[i];
        if (line->end_ms == WHOLE_CLIP || (ms >= line->start_ms && ms < line->end_ms + LINE_TAIL_MS)) {
            return line->text;
        }
    }
    return 0;
}

// --- Options --------------------------------------------------------------------------------------

// Enum options come back as the selected index (0-based), as the Skip Cutscene mod relies on.
static u32 opt(const char* key) { return (u32)recomp_get_config_u32(key); }

// Background opacity for the "Background Box" option (None/Light/Medium/Dark).
static u32 background_opacity(void) {
    switch (opt("background")) {
        case 1:  return 90;   // Light
        case 2:  return 160;  // Medium
        case 3:  return 220;  // Dark
        case 0:
        default: return 0;    // None
    }
}

// Text color for the "Text Color" option (White/Yellow/Cyan/Green).
static u32 text_color(void) {
    switch (opt("color")) {
        case 1:  return 0xFFE14A; // Yellow
        case 2:  return 0x4AE1FF; // Cyan
        case 3:  return 0x6CFF6C; // Green
        case 0:
        default: return 0xFFFFFF; // White
    }
}

static u32 build_style(void) {
    const u32 pos = opt("position"); // 0 bottom, 1 top, 2 middle (matches SUBTITLE_POS_*)
    const u32 size = opt("size");    // 0 small, 1 normal, 2 large (matches SUBTITLE_SIZE_*)
    return SUBTITLE_STYLE(pos, size, background_opacity());
}

// The debug view: "[0x015E1FD8 12.34s] " before the line, to write a clip's times by watching it.
static int debug_prefix(char* buf, int size, u32 rom, u32 ms) {
    static const char hex[] = "0123456789ABCDEF";
    int i = 0;
    const char* p = "[0x";
    while (*p && i < size - 1) buf[i++] = *p++;
    for (int shift = 28; shift >= 0 && i < size - 1; shift -= 4) buf[i++] = hex[(rom >> shift) & 0xF];
    if (i < size - 1) buf[i++] = ' ';
    char digits[12];
    int n = 0;
    u32 v = ms / 1000;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v != 0 && n < 10);
    while (n > 0 && i < size - 1) buf[i++] = digits[--n];
    const u32 cs = (ms % 1000) / 10;
    if (i < size - 6) {
        buf[i++] = '.';
        buf[i++] = (char)('0' + cs / 10);
        buf[i++] = (char)('0' + cs % 10);
        buf[i++] = 's';
        buf[i++] = ']';
        buf[i++] = ' ';
    }
    buf[i] = '\0';
    return i;
}

// --- Each game frame ------------------------------------------------------------------------------

// func_1501BBB8 runs once per game frame (it reads the controllers).
RECOMP_HOOK_RETURN("func_1501BBB8") void hollywood_on_frame(void) {
    static const char* shown = 0;   // the line up now (0: none)
    static int debug_shown = 0;     // the debug view is up
    static u32 seen_serial = 0;     // the last clip logged
    static u32 tail = 0;            // game frames since the clip stopped

    const u32 serial = clip_serial;
    const u32 rom = clip_rom;
    const u32 ms = clip_ms();
    const u32 state = g_mp3_state;
    const int stopped = state == MP3_IDLE || state == MP3_STOPPED;
    const SubClip* clip = find_clip(rom);

    if (serial != seen_serial) {
        seen_serial = serial;
        tail = 0;
        recomp_printf("[hollywood_subtitles] clip 0x%08X starts%s\n", (unsigned)rom, clip ? "" : " (no lines for it)");
    }
    tail = stopped ? tail + 1 : 0;

    const char* text = 0;
    if (opt("enabled") && clip != 0 && (clip->is_intro ? opt("intro_subtitles") : opt("general_subtitles")) != 0 &&
        serial != 0 && tail <= CLIP_TAIL_FRAMES) {
        text = text_at(clip, ms);
    }

    // The debug view while a known clip plays: its address and time, then the line (if any).
    if (opt("enabled") && opt("debug_segments") && clip != 0 && !stopped) {
        static char buf[160];
        int i = debug_prefix(buf, (int)sizeof(buf), rom, ms);
        for (const char* t = text ? text : ""; *t && i < (int)sizeof(buf) - 1; t++) buf[i++] = *t;
        buf[i] = '\0';
        recomp_subtitle_show(buf, build_style(), text_color());
        debug_shown = 1;
        shown = text;
        return;
    }
    if (debug_shown) {
        debug_shown = 0;
        if (text == 0) {
            recomp_subtitle_hide();
        }
        shown = (const char*)0;
        if (text != 0) {
            recomp_subtitle_show(text, build_style(), text_color());
            shown = text;
        }
        return;
    }

    if (text != shown) {
        if (text != 0) {
            recomp_subtitle_show(text, build_style(), text_color());
        } else {
            recomp_subtitle_hide();
        }
        shown = text;
    }
}
