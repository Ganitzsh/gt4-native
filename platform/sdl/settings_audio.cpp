// Audio: audio.* (lane D; docs/modern/PLAN.md D5, E8) (settings_table.h).
#include "settings_table.h"

namespace gt4::settings_table {

void audioOptions(std::vector<Option>& o) {
    o.push_back(enumOption("audio.mixer", &Settings::audioMixer, {"game", "host"},
        "Where GT4's sound effects and voices are mixed. game = in the game, as always. host = the game sends its "
        "sound processor's state and the host mixes the same voices (the step towards higher-quality and spatial "
        "sound); GT4 still reads its voices' state from the game, at once, so what it plays doesn't change "
        "(docs/modern/spikes/R16-audio-reply-timing.md). Music and movie sound are mixed in the game either way."));
    o.push_back(enumOption("audio.interpolation", &Settings::audioInterpolation, {"original", "cubic", "sinc"},
        "How the host's mixer reads a voice's sample between two of its recorded samples (with audio.mixer = host; "
        "the game's own mix is always original). original = straight lines between them, as this project always "
        "mixed: dull highs and some grit on pitched-up sounds. cubic = a smooth curve through four. sinc = a windowed "
        "sinc over eight, the closest to the recorded sound. GT4 plays the same sounds whichever is chosen."));
}

}  // namespace gt4::settings_table
