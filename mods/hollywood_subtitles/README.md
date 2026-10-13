# Hollywood Subtitles

Movie-style subtitles for **Conker's Bad Fur Day: Recompiled**.

The game is fully voice acted and draws no dialogue text: cutscenes show comic speech bubbles, not
words. This mod shows the spoken lines along the screen, like a film. The wording comes from a
transcript in the mod; **the timing comes from the game's own voice playback**: which recording it's
playing, and how far into it the voice is.

Covered so far:

- **Chainsaw boot** (scene `0x21`): "Stupid logo!", "There, much better.", "How can… get it… Ah!",
  "Marvelous."
- **Throne-room monologue** (scene `0x18`, the campaign intro): "Well. There I am. Conker the King…"
  through "Come closer, and I'll tell you." The script's last lines ("It all started…" to "…a bad fur
  day.") are in another recording, still to be found (see Status).

## Options (Mods menu)

| Option | What it does | Default |
|---|---|---|
| Enable Subtitles | Master switch. | On |
| Intro Cutscene Subtitles | The chainsaw boot and the throne-room monologue. | On |
| General Game Subtitles | The rest of the game's cutscenes (as the transcript grows). | On |
| Position / Text Size / Background Box / Text Color | How the subtitle looks. | Bottom / Normal / Medium / White |
| Debug: Voice Clips | Shows the recording's ROM address and how far into it the voice is (`[0x015E1FD8 12.34s]`) before each line, for writing the timings. | Off |

## How it works

The game's own code is untouched. Two parts:

1. **Host** (`host/src/subtitles.cpp`): a subtitle overlay, a recompui Label like the FPS counter. It
   exports `recomp_subtitle_show(text, style, color)` and `recomp_subtitle_hide()` to mods (see
   `mods/include/recompsubtitles.h`) and holds no subtitle logic of its own.
2. **Mod** (`src/hollywood_subtitles.c`): all the subtitle logic. It watches the game's MP3 voice player
   (Rare's library, named in [DevOldSchool's decomp](https://github.com/DevOldSchool/conkers-bfd-decomp)):
   - `mp3_play_file(romaddr, filesize)` (`func_151F2960`) starts each recording, with its ROM address.
     The mod hooks it to know which recording is playing.
   - `mp3main_continue_file` (`func_151F86B0`) decodes the recording's next MP3 frame: 576 samples at
     22,050 Hz, 26.1 ms. The mod counts them, so it knows how far into the recording the voice is,
     exactly. The count stops while the game is paused, as the player does.
   - Each game frame (the hook on `func_1501BBB8`), it shows the line whose time holds that position.

   The transcript lists each recording by ROM address, with its lines as times (ms) from the
   recording's start. A recording that is one line (most of the chainsaw boot) shows it for as long as
   it plays.

Measured on Windows: the throne monologue is one 38.8 s recording (ROM `0x015E1FD8`, 1,484 frames) in
every run, with two pauses or none, and its speech lands where it was measured from the recording's
own decoded audio. (The first version timed lines with the cutscene timer, 2 ticks a game frame. That
falls behind the voice whenever the game draws fewer than 30 frames a second, which is why its lines
drifted later in the monologue; its measured segment times were right.)

Two rules the code keeps:

- **The player's hooks only store numbers.** They run on the game's audio thread, which the runtime
  schedules with the game's other threads; printing or reading a clock there hung the game in testing.
- **No hook on a function `conker.toml` hooks** (a mod's hook replaces the host's): so none on
  `func_15095D34`, the speech bubbles' pieces, which the host hooks for widescreen and a crash fix.

## Adding lines

1. Turn on **Debug: Voice Clips** and play the scene. Each recording that starts is logged
   (`[hollywood_subtitles] clip 0x… starts (no lines for it)`), and while one with lines plays, its
   address and the voice's time show on screen.
2. Add the recording to `clips[]` with its lines as `{ start_ms, end_ms, "text" }`, or one
   `{ 0, WHOLE_CLIP, "text" }` for a recording that is one line.

## Status / open questions

- **The monologue's last lines** ("It all started… yesterday, and what a day that was! It's what I
  like to call… a bad fur day.") aren't in the monologue's recording, which ends at 38.8 s. The first
  version's offline decode read past its end into the data after it. The recording they're in needs
  finding (the log names each recording as it starts).
- **Hiding the speech bubbles** was dropped for now: it needs a way to tell speech bubbles from
  Conker's thought bubbles without hooking `func_15095D34`.
- The game's lip-sync records (`L:` records in 220 of the 453 recordings, passed to a callback at
  `0x800E0E00`) could fine-tune where lines start, but the monologue has none, so the timing doesn't
  rely on them.
- The transcript is built into the mod (a mod's MIPS code can't read files).

## Building

From the repository root (macOS needs `brew install llvm lld`):

```sh
sh install_mod.sh mods/hollywood_subtitles
```

builds the `.nrm` (via `mods/build_mod.sh`) and copies it into the game's data-folder `mods`
directory, which is where the game loads mods from. The host needs the subtitle overlay, so build it
with `./build.sh` too.
