# Recording a video

`gt4_host --record PATH` records what the host presents, with the game's sound, in real time: a README video, a
release trailer, a bug report. It is off unless `--record` is given, and it ships nothing: it runs the ffmpeg you have
(`ffmpeg` on PATH, or `--record-ffmpeg C:/path/to/ffmpeg.exe`), so install one first (on Windows, for example,
`winget install Gyan.FFmpeg`).

## How it works

- **Frames**: each presented image (what the window shows, or the offscreen image with `--headless`), HUD and host
  overlays included, is copied on the GPU to a buffer of its frame slot and handed over when that slot comes round
  again, a frame or two later, so the frame never waits for the copy. A writer thread pipes the frames to ffmpeg as raw
  video; the game doesn't wait for ffmpeg either: a frame that finds about 128 MB of frames still queued (ffmpeg
  behind) is dropped and counted, the frame before it shown again in its place so the video keeps real time.
- **Rate**: the video is a steady 60 fps placed by the wall clock. A frame presented late fills its gap by repeating;
  a frame presented faster than 60 Hz (a 120 Hz display) is skipped. The video keeps real time and stays with the
  sound. A fixed-step replay (`--replay` with `--fixed-step`, which draws as fast as it can) records each drawn frame
  as one video frame instead.
- **Sound**: the host's output mix (the game's own mix, or the host's SPU2 mixer with `audio.mixer = host`), also
  headless or in a session where nothing plays it, is written beside the output while recording. When the host
  closes, ffmpeg muxes the two: the video as encoded, the sound as AAC 192 kbit/s. `--no-audio` only stops playback:
  the recording keeps the sound.
- **Encoding**: `--record-args "ARGS"` replaces the video encoder's options, by default
  `-c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p` (CPU). Any encoder ffmpeg has works, for example
  `--record-args "-c:v h264_nvenc -preset p5 -cq 19 -pix_fmt yuv420p"` (NVIDIA). A GPU encoder is not faster by
  itself here: ffmpeg still converts each frame to YUV on the CPU (on the 2026-11-09 test, AMD's `h264_amf` dropped
  more frames than `libx264 -preset veryfast`); compare the end line's drops before choosing.
- **The end**: the host prints what it wrote, for example `Recorded out.mkv: 2700 frames presented, 2700 video
  frames written (45.00 s at 60 fps): 10 repeats filling late presents, 4 dropped (ffmpeg behind: the frame before
  shown again), 10 skipped (drawn faster than 60 fps); sound 45.03 s`. Repeats mean the host presented late; drops
  mean ffmpeg could not keep up (try a faster `-preset`). Both show as a frame held twice; the video's length stays the
  wall clock's. If ffmpeg or the mux fails, the two intermediate files (`PATH.video.mkv`, `PATH.sound.raw`) are kept
  and the command is printed.
- The window keeps one size while recording: frames of another size (a resize, fullscreen toggled) are left out.
  8-bit and 10-bit outputs are recorded; an HDR output in 16-bit floats (scRGB) is refused with a warning, and a
  10-bit HDR one records its PQ signal as plain video: record with `display.hdr = off`.
- On Windows ffmpeg starts through cmd.exe, so `--record` and `--record-ffmpeg` paths with `%` or `"` are refused.

## A README video at 1080p60

From the folder that holds `gt4_host.exe` and `gt4_game.exe` (a release package, or a clone's build):

```
gt4_host.exe --game gt4_game.exe --game-args "--elf C:/path/to/CORE.GT4.elf" --saves C:/path/to/a/save/copy
             --set display.window_width=1920 --set display.window_height=1080 --set display.vsync=on
             --set debug.overlay=off --record readme.mkv
```

Play, then close the window: the recording is finished when the host prints `Recorded readme.mkv: ...`. Without a
window, at exactly 1920x1080 and with no desktop involved:

```
gt4_host.exe --headless --realtime --size 1920x1080 --game gt4_game.exe --game-args "--elf ..." --saves ...
             --record readme.mkv --frames 3600
```

(`--realtime`: a headless host otherwise steps one tick a frame as fast as it can; `--frames 3600`: one minute.)
A session (`--session-dir`, tools/host_session.py) drives the game from a script for a recording that can be made
again the same way. Keep the save a copy: the game writes to it.

Cut the start and the end afterwards without re-encoding, for example
`ffmpeg -ss 5 -to 35 -i readme.mkv -c copy readme-cut.mkv`, and for a web page
`ffmpeg -i readme-cut.mkv -c:v libx264 -crf 22 -preset slow -c:a aac -b:a 160k -movflags +faststart readme.mp4`.
