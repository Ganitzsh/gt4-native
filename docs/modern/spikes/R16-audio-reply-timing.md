# R16: how late GT4's SPU status can come (lane D, 2026-11-08)

RISKS.md R16 asks how late the ENDX / ENVX replies can arrive before GT4 runs short of voices or cuts sounds, if the
mixer moves to the host (PLAN.md D5). Answer: **not even one frame.** GT4's sound driver keys voices again from what
the status says, and with a status one frame late it plays different sounds. D5 therefore keeps the status exact, in
the same SPUP call, from a voice model on the game side; only the mixing moves to the host.

## How it was measured

- `GT4_SPU_REPLY_DELAY=N` (`runtime/audio.cpp`, debugging; 0-8) answers each SPUP status (fno 3 and 4, one call a
  VBlank) with the status of N calls before. Unset, the status is the current one, as always. 0 keeps the current
  status but turns the counts on.
- The game log, every 600 frames, counts since the start:
  - key-ons;
  - key-ons onto a voice still keyed (attack, decay or sustain), and onto one releasing (its tail cut);
  - key-ons of a voice 1, 2, 3 or 4 frames after its previous key-on.
- The run: the approved save (a fresh copy, deleted after), the GT-mode route to the race
  (`docs/modern/routes/race_gt_pe2.txt`), then the same inputs every time: full throttle, steering pulses into the
  walls, a brake every third pulse, then coasting. Headless, 9,600 frames; the race runs from about frame 3,600.
- Six runs at delay 0, 1, 2 and 4, then three more (0, 1, 2) with the key-on gaps. A repeated delay gives the same
  totals within 2 % on one build (delay 0: 24,272 and 24,764 key-ons; delay 1: 45,661 and 46,153). The second
  round's build (the same game plus the gap counts) gave 4 % fewer at delay 0 and 8 % fewer at delay 1: runs vary
  by that much, far less than the delays' effect.

## Results

Totals at the last count of each run (frame 7,800; the route quits soon after), all from the start:

| delay | key-ons | onto a voice still keyed | onto one releasing |
|---|---|---|---|
| 0 | 24,272 / 24,764 / 23,796 | 22,796 / 23,400 / 22,428 | 57 / 55 / 56 |
| 1 | 45,661 / 46,153 / 42,439 | 44,522 / 45,009 / 41,272 | 249 / 265 / 226 |
| 2 | 60,835 / 56,999 | 59,823 / 55,913 | 281 / 290 |
| 4 | 59,746 | 58,702 | 293 |

The menus before the race are the same at every delay (about 220 key-ons by frame 3,000). The race is not:

- **One frame late almost doubles the key-ons** (+86-88 %), two frames late gives 2.4-2.5 times; four is no worse
  than two.
- **Key-ons onto a releasing voice grow four to five times** (about 56 to 230-290): sound tails cut short.
- Most key-ons land on a voice still keyed at every delay (94-98 %). That is GT4's own way at delay 0 too (it keys
  some voices again every few frames), so it is not by itself a symptom.

The key-on gaps show the mechanism (the second round, frame 7,800):

| delay | again after 1 frame | 2 frames | 3 frames | 4 frames |
|---|---|---|---|---|
| 0 | 7,478 | 9,671 | 3,937 | 178 |
| 1 | 9,278 | 4,566 | 24,973 | 2,175 |
| 2 | 9,514 | 9,594 | 4,289 | 31,418 |

The driver's main re-key gap moves by exactly the delay: 2 frames at delay 0, 3 at delay 1, 4 at delay 2. And it
happens about 2.6 and 3.2 times as often. GT4 keys some voices again when the status says they have stopped. A late
status still shows a voice as it was before its last key-on, so the driver keys it once more, and more of the
voice's sounds start over. No voice ran short in these runs (the driver keeps using the voices it has), but every delay
changes what is heard.

## What D5 does about it

The status has to describe the voices as they are when GT4 asks, in the same SPUP call, as it does now. So:

- **On the game side**, a voice model answers the status: each voice's key state, ADSR envelope (ENVX), and its
  address advancing at its pitch through the ADPCM blocks' loop and end flags (ENDX). That is what `audio.cpp`
  already computes, without the sample decoding, interpolation, volume, reverb and the mix.
- **On the host**, the mixer plays the same voices from the register image (or the key-on, pitch, volume and
  key-off changes in it). Its latency then changes only when a sound is heard, not what GT4 plays.
- The two run the same envelope and address rules from the same register image, so a voice ends at the same frame
  on both. The game side never waits for the host.
- D5's acceptance line "no voice frees early" then holds by construction. The check stays useful:
  `GT4_SPU_REPLY_DELAY=0` with the counts above must give the same totals as the current game-side mixer.

R18 (the sound driver without the register image) is not changed by this: it needs the same exact status from
whatever replaces the image.
