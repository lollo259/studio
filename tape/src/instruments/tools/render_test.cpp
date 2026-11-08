// Tape instruments: offline test. Plays a short phrase on an SFZ instrument and writes a WAV.
// usage: render_test <instrument.sfz> <out.wav>
#include <sfizz.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cmath>

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    FILE* f = std::fopen(path, "wb");
    const uint32_t n = l.size(), dataBytes = n * 2 * 2;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(dataBytes);
    for (uint32_t i = 0; i < n; ++i) {
        for (float s : { l[i], r[i] }) { s = std::fmax(-1.f, std::fmin(1.f, s)); w16((uint16_t)(int16_t)(s * 32767)); }
    }
    std::fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s inst.sfz out.wav\n", argv[0]); return 1; }
    const int rate = 48000, block = 256;
    sfz::Sfizz synth;
    synth.setSampleRate(rate);
    synth.setSamplesPerBlock(block);
    if (!synth.loadSfzFile(argv[1])) { std::fprintf(stderr, "could not load %s\n", argv[1]); return 2; }
    std::printf("regions: %d\n", synth.getNumRegions());

    // phrase: (beat, note, velocity, length in beats) at 72 bpm
    struct N { double at; int key; int vel; double len; };
    const std::vector<N> notes = {
        {0, 48, 70, 4}, {0, 55, 60, 4}, {0, 64, 72, 1}, {1, 67, 68, 1}, {2, 72, 75, 2},
        {4, 45, 70, 4}, {4, 52, 60, 4}, {4, 60, 72, 1}, {5, 64, 66, 1}, {6, 69, 78, 2},
        {8, 41, 70, 4}, {8, 48, 60, 4}, {8, 57, 70, 4}, {8, 60, 64, 4}, {8, 65, 80, 4},
    };
    const double spb = 60.0 / 72.0;
    const int total = int((12 * spb + 3.0) * rate);
    std::vector<float> L(total), R(total), bl(block), br(block);
    for (int pos = 0; pos < total; pos += block) {
        for (const N& n : notes) {
            int on = int(n.at * spb * rate), off = int((n.at + n.len) * spb * rate);
            if (on >= pos && on < pos + block) synth.noteOn(on - pos, n.key, n.vel);
            if (off >= pos && off < pos + block) synth.noteOff(off - pos, n.key, 0);
        }
        float* out[2] = { bl.data(), br.data() };
        synth.renderBlock(out, block);
        for (int i = 0; i < block && pos + i < total; ++i) { L[pos + i] = bl[i]; R[pos + i] = br[i]; }
    }
    writeWav(argv[2], L, R, rate);
    std::printf("wrote %s (%.1f s)\n", argv[2], total / double(rate));
    return 0;
}
