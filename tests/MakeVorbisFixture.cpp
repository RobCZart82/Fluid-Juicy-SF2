#include <sndfile.h>
#include <cmath>
#include <iostream>
#include <vector>

// Original 440 Hz sine, matching make_fixture.py; no third-party audio.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    SF_INFO info{};
    info.samplerate = 44100;
    info.channels = 1;
    info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    auto* file = sf_open(argv[1], SFM_WRITE, &info);
    if (!file) {
        std::cerr << sf_strerror(nullptr) << '\n';
        return 1;
    }
    std::vector<short> samples(44100);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<short>(std::lround(10000 * std::sin(2 * 3.14159265358979323846 * 440 * i / 44100)));
    const auto written = sf_writef_short(file, samples.data(), static_cast<sf_count_t>(samples.size()));
    const auto closed = sf_close(file);
    if (written != static_cast<sf_count_t>(samples.size()) || closed != 0) {
        std::cerr << "Could not write the complete original Vorbis fixture\n";
        return 1;
    }
    return 0;
}
