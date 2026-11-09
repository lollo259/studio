/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "engines.h"

#include <sfizz.hpp>
#include <fluidsynth.h>

using namespace au::instruments;

namespace {
class SfzEngine : public Engine
{
public:
    SfzEngine(float rate, int maxBlock)
    {
        m_s.setSampleRate(rate);
        m_s.setSamplesPerBlock(maxBlock);
        m_s.setNumVoices(96);
        m_s.setVolume(0.0f); // sfizz turns everything down 7 dB by default; Tape plays at full level
    }

    sfz::Sfizz& synth() { return m_s; }

    void setSampleRate(float rate) override { m_s.setSampleRate(rate); }
    void noteOn(int note, int velocity) override { m_s.noteOn(0, note, velocity); }
    void noteOff(int note) override { m_s.noteOff(0, note, 0); }
    void cc(int cc, int value) override { m_s.cc(0, cc, value); }
    void pitchWheel(int value) override { m_s.pitchWheel(0, value); }
    void allSoundOff() override { m_s.allSoundOff(); }
    void render(float* left, float* right, int n) override
    {
        float* b[2] = { left, right };
        m_s.renderBlock(b, n, 1);
    }

private:
    sfz::Sfizz m_s;
};

class FluidEngine : public Engine
{
public:
    ~FluidEngine() override
    {
        if (m_synth) {
            delete_fluid_synth(m_synth);
        }
        if (m_settings) {
            delete_fluid_settings(m_settings);
        }
    }

    bool load(float rate, const std::string& file, int bank, int program)
    {
        m_settings = new_fluid_settings();
        if (!m_settings) {
            return false;
        }
        fluid_settings_setnum(m_settings, "synth.sample-rate", rate);
        fluid_settings_setint(m_settings, "synth.polyphony", 256);
        fluid_settings_setint(m_settings, "synth.reverb.active", 1);
        fluid_settings_setint(m_settings, "synth.chorus.active", 0);
        fluid_settings_setnum(m_settings, "synth.gain", 0.6);
        m_synth = new_fluid_synth(m_settings);
        if (!m_synth) {
            return false;
        }
        m_sfont = fluid_synth_sfload(m_synth, file.c_str(), 0);
        if (m_sfont == FLUID_FAILED) {
            return false;
        }
        m_rate = rate;
        m_bank = bank;
        m_program = program;
        if (bank >= 120) {
            fluid_synth_set_channel_type(m_synth, 0, CHANNEL_TYPE_DRUM);
        }
        return fluid_synth_program_select(m_synth, 0, m_sfont, bank, program) == FLUID_OK;
    }

    void setSampleRate(float rate) override
    {
        if (rate != m_rate) {
            fluid_settings_setnum(m_settings, "synth.sample-rate", rate);
            // FluidSynth reads the rate when the synth is made: the voices keep working, only the pitch
            // would be off, so rebuild the synth in place
            fluid_synth_t* s = new_fluid_synth(m_settings);
            if (s) {
                const int id = fluid_synth_sfload(s, fluid_sfont_get_name(fluid_synth_get_sfont_by_id(m_synth, m_sfont)), 0);
                if (id != FLUID_FAILED) {
                    if (m_bank >= 120) {
                        fluid_synth_set_channel_type(s, 0, CHANNEL_TYPE_DRUM);
                    }
                    fluid_synth_program_select(s, 0, id, m_bank, m_program);
                    delete_fluid_synth(m_synth);
                    m_synth = s;
                    m_sfont = id;
                    m_rate = rate;
                } else {
                    delete_fluid_synth(s);
                }
            }
        }
    }

    void noteOn(int note, int velocity) override { fluid_synth_noteon(m_synth, 0, note, velocity); }
    void noteOff(int note) override { fluid_synth_noteoff(m_synth, 0, note); }
    void cc(int cc, int value) override { fluid_synth_cc(m_synth, 0, cc, value); }
    void pitchWheel(int value) override { fluid_synth_pitch_bend(m_synth, 0, value + 8192); }
    void allSoundOff() override { fluid_synth_all_sounds_off(m_synth, -1); }
    void render(float* left, float* right, int n) override
    {
        fluid_synth_write_float(m_synth, n, left, 0, 1, right, 0, 1);
    }

private:
    fluid_settings_t* m_settings = nullptr;
    fluid_synth_t* m_synth = nullptr;
    int m_sfont = FLUID_FAILED;
    float m_rate = 0;
    int m_bank = 0;
    int m_program = 0;
};
}

Engine* au::instruments::makeSfzEngine(float sampleRate, int maxBlock, const std::string& file)
{
    auto* e = new SfzEngine(sampleRate, maxBlock);
    if (!e->synth().loadSfzFile(file)) {
        delete e;
        return nullptr;
    }
    return e;
}

Engine* au::instruments::makeSfzEngineFromText(float sampleRate, int maxBlock, const std::string& virtualPath, const std::string& text)
{
    auto* e = new SfzEngine(sampleRate, maxBlock);
    if (!e->synth().loadSfzString(virtualPath, text)) {
        delete e;
        return nullptr;
    }
    return e;
}

Engine* au::instruments::makeSoundFontEngine(float sampleRate, const std::string& file, int bank, int program)
{
    auto* e = new FluidEngine();
    if (!e->load(sampleRate, file, bank, program)) {
        delete e;
        return nullptr;
    }
    return e;
}
