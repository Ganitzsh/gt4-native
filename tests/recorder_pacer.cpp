// --record's frame pacing (platform/sdl/recorder.h): frames presented at wall-clock times become a fixed-rate video
// that keeps real time. One copy for a frame on time, none for one drawn faster than the rate, more for a late one.
#include "recorder.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    const double frame = 1.0 / 60.0;
    {
        gt4::FramePacer p(60.0);
        expect(p.copies(0.0) == 1, "the first frame is written once");
        bool steady = true;
        for (int n = 1; n < 600; ++n) steady = steady && p.copies(n * frame + 0.002) == 1;   // ten seconds on time
        expect(steady, "frames on time are written once each");
        expect(p.written() == 600, "ten seconds at 60 Hz make 600 video frames");
    }
    {
        gt4::FramePacer p(60.0);
        p.copies(0.0);
        expect(p.copies(0.25 * frame) == 0, "a frame drawn a quarter frame later is skipped (a 240 Hz display)");
        expect(p.copies(1.0 * frame + 0.001) == 1, "the next frame on time is written");
    }
    {
        gt4::FramePacer p(60.0);
        p.copies(0.0);
        expect(p.copies(3.5 * frame) == 3, "a frame 3.5 frames late fills the gap: written three times");
        expect(p.copies(4.0 * frame + 0.001) == 1, "and the one after is on time again");
        expect(p.written() == 5, "the video stays at the wall clock (five frames by 4/60 s)");
    }
    {
        gt4::FramePacer p(60.0);
        expect(p.copies(-0.001) == 1, "a time before the start counts as the start");
    }
    {
        gt4::FramePacer p(30.0);
        p.copies(0.0);
        expect(p.copies(frame) == 0 && p.copies(2 * frame + 0.001) == 1, "at 30 fps every other 60 Hz frame is kept");
    }
    if (failures) return 1;
    std::printf("recorder pacing: ok\n");
    return 0;
}
