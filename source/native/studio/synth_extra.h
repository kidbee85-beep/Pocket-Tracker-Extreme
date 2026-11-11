// Original PocketTracker Studio oscillators and synthetic granular sources.
// GPL-3.0-or-later. No recorded samples or additional third-party DSP.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "synth_params.h"

namespace studio {
namespace extra {
constexpr float sampleRate = 48000.f;
inline float wrap(float x) { return x - std::floor(x); }
inline float sine(float phase) {
    const float x = 2.f * wrap(phase) - 1.f;
    const float y = 4.f * x * (1.f - std::fabs(x));
    return -(y + .225f * (y * std::fabs(y) - y));
}
inline float triangle(float phase) { return 1.f - 4.f * std::fabs(wrap(phase) - .5f); }
inline float blep(float phase, float step) {
    if (phase < step) { const float t = phase / step; return t + t - t * t - 1.f; }
    if (phase > 1.f - step) { const float t = (phase - 1.f) / step; return t * t + t + t + 1.f; }
    return 0.f;
}
inline float saw(float phase, float step) { return 2.f * phase - 1.f - blep(phase, step); }
inline float pulse(float phase, float step, float width) {
    return (phase < width ? 1.f : -1.f) + blep(phase, step) - blep(wrap(phase - width), step);
}

// Five octave-spaced partials with overlapping amplitude windows. When the
// pitch sweep wraps, move each oscillator phase with its still-audible partial.
// Only the incoming silent edge partial starts a new phase.
class OctaveOrbit {
public:
    void reset() {
        position_ = 0.f;
        anchorPhase_ = 0.f;
        phases_.fill(0.f);
    }

    float next(float step, float rate, float spread, float anchor) {
        position_ += octave_orbit_rate(rate) / sampleRate;
        if (position_ >= 1.f) {
            position_ -= 1.f;
            for (size_t i = phases_.size() - 1; i > 0; --i) phases_[i] = phases_[i - 1];
            phases_.front() = 0.f;
        } else if (position_ < 0.f) {
            position_ += 1.f;
            for (size_t i = 0; i + 1 < phases_.size(); ++i) phases_[i] = phases_[i + 1];
            phases_.back() = 0.f;
        }
        float increment = step * .25f * std::exp2(position_);
        const float inverseWidth = 1.f / (1.15f + 1.35f * spread);
        float level = 0.f, sum = 0.f;
        for (size_t i = 0; i < phases_.size(); ++i) {
            const float distance = std::fabs(float(i) + position_ - 2.5f);
            const float window = std::max(0.f, 1.f - distance * inverseWidth);
            const float band = std::clamp((.44f - increment) * 36.f, 0.f, 1.f);
            const float weight = window * window * band;
            phases_[i] = wrap(phases_[i] + increment);
            if (weight > 0.f) sum += sine(phases_[i]) * weight;
            level += weight;
            increment *= 2.f;
        }
        anchorPhase_ = wrap(anchorPhase_ + step);
        const float rootMix = .75f * anchor;
        return .56f * ((1.f - rootMix) * sum / std::max(.4f, level) + rootMix * sine(anchorPhase_));
    }

private:
    float position_ = 0.f;
    float anchorPhase_ = 0.f;
    std::array<float, 5> phases_{};
};

// Motion determines when each resonator is struck. The played note is the
// root, and later impacts choose intervals from the selected scale.
class GravityVoice {
public:
    void start(int model) {
        model_ = model;
        first_ = true;
        height_ = 1.f;
        velocity_ = 0.f;
        orbit_ = 0.f;
        drops_ = {.13f, .29f, .47f};
        bells_ = {};
        previousDegree_ = 0;
    }

    void set_scale(int scale) { scale_ = std::clamp(scale, 0, 3); }

    float next(float step, float force, float motion, float spread) {
        if (first_) {
            strike(step, 0.f, .7f, .08f + .19f * motion);
            first_ = false;
        }
        const float acceleration = 5.f + 105.f * force * force;
        if (model_ == 0) bounce(step, acceleration, motion, spread);
        else if (model_ == 1) orbit(step, force, motion, spread);
        else rain(step, acceleration, motion, spread);

        float sum = 0.f;
        float envelopeSum = 0.f;
        for (auto& bell : bells_) {
            if (bell.envelope < .00012f) continue;
            const float frequency = step * bell.ratio;
            const float overtone = frequency * (model_ == 0 ? 2.01f : model_ == 1 ? 2.756f : 3.51f);
            bell.phase = wrap(bell.phase + frequency);
            bell.overtonePhase = wrap(bell.overtonePhase + overtone);
            bell.attack = std::min(1.f, bell.attack + 1.f / 132.f);
            const float gain = bell.attack * bell.envelope;
            const float bright = model_ == 0 ? .21f : model_ == 1 ? .34f : .28f;
            const float overtoneLevel = std::clamp((.44f - overtone) * 36.f, 0.f, 1.f);
            sum += gain * (sine(bell.phase) + bright * overtoneLevel * sine(bell.overtonePhase));
            envelopeSum += gain;
            bell.envelope *= bell.decay;
        }
        return .47f * sum / std::max(1.f, envelopeSum);
    }

private:
    struct Bell {
        float phase = 0.f, overtonePhase = 0.f, attack = 0.f;
        float ratio = 1.f, envelope = 0.f, decay = 0.f;
    };

    uint32_t random() {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return random_;
    }

    float unit_random() { return float(random() >> 8) * (1.f / 16777216.f); }

    int semitone(float spread) {
        static constexpr int intervals[][12] = {
            {0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}, {0, 2, 3, 5, 7, 9, 10},
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
        };
        static constexpr int length[] = {5, 5, 7, 12};
        const int degrees = length[scale_];
        const int maximum = int(std::lround(spread * 2.f * degrees));
        if (maximum == 0) return 0;
        int degree = int(random() % unsigned(maximum + 1));
        if (degree == previousDegree_) degree = (degree + 1) % (maximum + 1);
        previousDegree_ = degree;
        return 12 * (degree / degrees) + intervals[scale_][degree % degrees];
    }

    void strike(float step, float spread, float strength, float decaySeconds) {
        auto quietest = std::min_element(bells_.begin(), bells_.end(),
                                         [](const auto& a, const auto& b) { return a.envelope < b.envelope; });
        // Leave audible tails alone at very dense settings.
        if (quietest->envelope > .07f) return;
        float ratio = std::exp2(float(semitone(spread)) / 12.f);
        while (step * ratio > .36f) ratio *= .5f;
        *quietest = {0.f, 0.f, 0.f, ratio, strength, std::exp(-1.f / (decaySeconds * sampleRate))};
    }

    void bounce(float step, float acceleration, float rebound, float spread) {
        velocity_ -= acceleration / sampleRate;
        height_ += velocity_ / sampleRate;
        if (height_ > 0.f) return;
        const float energy = std::min(1.f, -velocity_ / std::sqrt(2.f * acceleration));
        strike(step, spread, .29f + .43f * energy, .028f + .115f * rebound);
        velocity_ = -velocity_ * (.38f + .48f * rebound);
        // Reintroduce the ball when the next bounce would be less than 44 ms.
        if (velocity_ < acceleration * .022f) {
            height_ = .65f + .35f * unit_random();
            velocity_ = 0.f;
        } else height_ = 0.f;
    }

    void orbit(float step, float speed, float eccentricity, float spread) {
        const int previousSector = int(orbit_ * 4.f);
        const float near = .5f + .5f * sine(orbit_ + .25f);
        const float angularSpeed = (.26f + 2.54f * speed * speed) *
                                   (1.f + eccentricity * .72f * (2.f * near - 1.f));
        orbit_ = wrap(orbit_ + angularSpeed / sampleRate);
        const int sector = int(orbit_ * 4.f);
        if (previousSector != sector) {
            strike(step, spread, .39f + .24f * near, .073f + .24f * (1.f - near));
        }
    }

    void rain(float step, float acceleration, float density, float spread) {
        const int streams = density < .33f ? 1 : density < .67f ? 2 : 3;
        for (int i = 0; i < streams; ++i) {
            drops_[i] -= 1.f / sampleRate;
            if (drops_[i] > 0.f) continue;
            const float height = .15f + .9f * unit_random();
            drops_[i] = std::sqrt(2.f * height / acceleration) * (1.34f - .68f * density);
            strike(step, spread, .19f + .39f * unit_random(), .034f + .075f * height);
        }
    }

    int model_ = 0, scale_ = 0, previousDegree_ = 0;
    bool first_ = true;
    float height_ = 1.f, velocity_ = 0.f, orbit_ = 0.f;
    uint32_t random_ = 0xa0761d65u;
    std::array<float, 3> drops_{};
    std::array<Bell, 4> bells_{};
};

struct Grain {
    float phase = 0, increment = 0, age = 0, ageStep = 1, amplitude = 0;
};

// Twenty KiB per Synth, fixed-size and allocation-free in the callback. Granular models read a
// synthesized periodic source through independently windowed grain heads, never user audio.
class Voice {
public:
    void start(int mode, float a, float b, float c) {
        mode_ = mode; pending_ = true; counter_ = 0; grainIndex_ = 0;
        for (auto& g : grains_) g.amplitude = 0;
        if (mode < 8) {
            for (size_t i = 0; i < table_.size(); ++i) {
                const float p = float(i) / float(table_.size());
                float harmonic = sine(p) + .35f * a * sine(p * 3.f) + .20f * a * sine(p * 7.f);
                const float texture = .6f * triangle(p) + .4f * (2.f * p - 1.f);
                const float noiseMix = mode == 5 ? .18f + .5f * a : mode == 6 ? .08f * a : .025f * a;
                table_[i] = ((1.f - a) * harmonic + a * texture) * (1.f - noiseMix) + noise() * noiseMix;
            }
        }
        if (mode == 13 || mode == 14) {
            delay_.fill(0.f); write_ = 0; low_ = 0; allpass_ = 0;
        }
        if (mode == 18) orbit_.reset();
        if (mode >= 19) gravity_.start(mode - 19);
        for (int h = 1; h <= 12; ++h) {
            float gain = std::pow(float(h), -.35f - 2.6f * a);
            if ((h & 1) == 0) gain *= b;
            partialGain_[size_t(h - 1)] = gain;
        }
        const float formants[] = {280.f + a * 620.f, 700.f + b * 2100.f, 2700.f + c * 900.f};
        for (int i = 0; i < 3; ++i)
            formantF_[size_t(i)] = 2.f * sine(std::min(.22f, formants[i] / sampleRate * .5f));
        previousA_=a;previousB_=b;previousC_=c;noiseOverride_=-1;
    }

    void set_gravity_scale(int scale) { gravity_.set_scale(scale); }

    // Derive only coefficients/source content. Active grain heads, oscillator phase, envelopes,
    // Karplus delay and the formant filter histories survive a live edit.
    void update(float a,float b,float c,const SynthParams& params) {
        if(mode_<8&&(a!=previousA_||params[3]!=noiseOverride_)){
            for(size_t i=0;i<table_.size();++i){
                const float p=float(i)/float(table_.size());
                const float harmonic=sine(p)+.35f*a*sine(p*3)+.20f*a*sine(p*7);
                const float texture=.6f*triangle(p)+.4f*(2*p-1);
                const float authored=mode_==5?.18f+.5f*a:mode_==6?.08f*a:.025f*a;
                const float noiseMix=params[3]<0?authored:params[3]/255.f*.9f;
                table_[i]=((1-a)*harmonic+a*texture)*(1-noiseMix)+noise()*noiseMix;
            }
        }
        if(a!=previousA_||b!=previousB_){
            for(int h=1;h<=12;++h){float gain=std::pow(float(h),-.35f-2.6f*a);if((h&1)==0)gain*=b;partialGain_[size_t(h-1)]=gain;}
        }
        if(a!=previousA_||b!=previousB_||c!=previousC_){
            const float frequencies[]={280+a*620,700+b*2100,2700+c*900};
            for(int i=0;i<3;++i)formantF_[size_t(i)]=2*sine(std::min(.22f,frequencies[i]/sampleRate*.5f));
        }
        previousA_=a;previousB_=b;previousC_=c;noiseOverride_=params[3];
        density_=params[0]<0?-1.f:params[0]/255.f;
        spread_=params[1]<0?-1.f:params[1]/255.f;
        grainGain_=params[2]<0?1.f:params[2]/255.f*2.f;
    }

    float next(float hz, float a, float b, float c) {
        hz = std::clamp(hz, 12.f, 14000.f);
        const float step = hz / sampleRate;
        float value = mode_ < 8 ? grain(hz, a, b, c) : oscillator(hz, step, a, b, c);
        pending_ = false;
        // Remove bias introduced by asymmetrical windows, fold bias and comb excitation.
        const float filtered = value - previous_ + .9993f * dc_;
        previous_ = value; dc_ = filtered;
        return std::isfinite(filtered) ? std::clamp(filtered, -1.4f, 1.4f) : 0.f;
    }

private:
    float noise() {
        random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
        return float(random_ >> 8) / 8388608.f - 1.f;
    }
    float table(float p) const {
        const float index = wrap(p) * float(table_.size());
        const int i = int(index);
        const float frac = index - i;
        return table_[size_t(i)] + (table_[size_t((i + 1) & 1023)] - table_[size_t(i)]) * frac;
    }
    void spawn(float hz, float a, float b, float c) {
        auto& g = grains_[size_t(grainIndex_++ & 15)];
        float length = 240.f + b * b * 7200.f;
        float ratio = 1.f;
        const int sequence = grainIndex_ & 3;
        if (mode_ == 1) ratio = sequence == 0 ? 4.f : sequence == 2 ? 2.f : 1.f;
        if (mode_ == 3) { constexpr float chord[] = {1.f, 1.259921f, 1.498307f, 2.f}; ratio = chord[sequence]; }
        if (mode_ == 4) { ratio = std::min(2.f + 14.f * a, 20000.f / hz); length = std::clamp((1.f + 7.f * b) * sampleRate / hz, 16.f, 4800.f); }
        if (mode_ == 5) ratio = std::exp2(noise() * (spread_<0?c*.65f:spread_*1.5f));
        if (mode_ == 6) ratio = 1.f + (1.f + 7.f * a) * float(sequence);
        if (mode_ == 7) { length = 180.f + b * 1100.f; ratio = sequence == 3 ? .5f : 1.f; }
        const float spread = spread_<0?(mode_ == 0 ? c * .012f : mode_ == 3 ? c * .007f : c * .025f):spread_*.08f;
        ratio *= 1.f + spread * noise();
        g.phase = mode_ == 4 ? 0.f : mode_ == 7 ? a * .5f : wrap(.5f + noise() * .5f);
        g.increment = std::min(.43f, hz / sampleRate * ratio) * (mode_ == 2 || (mode_ == 7 && sequence == 2) ? -1.f : 1.f);
        g.age = 0; g.ageStep = 1.f / length;
        g.amplitude = mode_ == 1 ? .13f : mode_ == 7 ? .58f : .22f;
        counter_ += density_>=0?length/(.5f+23.5f*density_):mode_ == 4 ? sampleRate / hz : mode_ == 7 ? length * (.9f + .8f * c) : length / (3.f + 10.f * c);
    }
    float grain(float hz, float a, float b, float c) {
        if (counter_ <= 0.f) spawn(hz, a, b, c);
        counter_ -= 1.f;
        float sum = 0;
        for (auto& g : grains_) if (g.amplitude > 0.f) {
            // Hann window, with an exponential variant for resonant hit clouds.
            float window = .5f - .5f * sine(g.age + .25f);
            if (mode_ == 6) window *= 1.f - .7f * g.age;
            const float source = mode_ == 6 ? sine(g.phase) : table(g.phase);
            sum += source * window * g.amplitude;
            g.phase = wrap(g.phase + g.increment); g.age += g.ageStep;
            if (g.age >= 1.f) g.amplitude = 0.f;
        }
        return sum*grainGain_;
    }
    float delayed(float length) const {
        const float index = wrap((float(write_) - length) / float(delay_.size())) * float(delay_.size());
        const int i = int(index); const float frac = index - i;
        return delay_[size_t(i)] + (delay_[size_t((i + 1) & 4095)] - delay_[size_t(i)]) * frac;
    }
    float oscillator(float hz, float step, float a, float b, float c) {
        float sum = 0;
        switch (mode_ - 8) {
        case 0: { // Seven band-limited detuned saws, variable centre/side balance.
            for (int i = 0; i < 7; ++i) {
                const float detune = (i - 3) * (.0006f + a * .006f);
                const float inc = std::min(.43f, step * (1.f + detune));
                phases_[size_t(i)] = wrap(phases_[size_t(i)] + inc);
                sum += saw(phases_[size_t(i)], inc) * (i == 3 ? 1.f : .25f + b * .75f);
            }
            phase_ = wrap(phase_ + step * .5f);
            sum = sum * .16f + sine(phase_) * c * .10f;
        } break;
        case 1: { // Two-dimensional waveform vector with phase-rotated sub voice.
            phase_ = wrap(phase_ + step);
            const float s = sine(phase_), t = triangle(phase_);
            const float p = pulse(phase_, step, .5f), w = saw(phase_, step);
            sum = ((s + (t - s) * a) * (1.f - b) + (p + (w - p) * a) * b) * .48f;
            sum = sum * (1.f - c * .35f) + sine(phase_ + c * .25f) * c * .18f;
        } break;
        case 2: { // Two-operator phase FM, integer ratio, bounded feedback.
            phase_ = wrap(phase_ + step);
            const float ratio = 1.f + std::floor(a * 7.99f);
            phase2_ = wrap(phase2_ + step * ratio);
            const float mod = sine(phase2_ + last_ * c * .18f);
            last_ = sine(phase_ + mod * b * 2.8f);
            sum = last_ * .55f;
        } break;
        case 3: { // Three-oscillator ring bank with an independent fundamental.
            phase_ = wrap(phase_ + step);
            phase2_ = wrap(phase2_ + step * (1.f + std::floor(a * 6.f)));
            phase3_ = wrap(phase3_ + step * (1.5f + b * 3.5f));
            sum = sine(phase_) * (sine(phase2_) * (.6f - .3f * c) + sine(phase3_) * .3f * c) + sine(phase_) * .15f;
        } break;
        case 4: { // Twelve-partial additive spectrum: tilt, odd/even and phase warp.
            phase_ = wrap(phase_ + step);
            float weight = 0;
            for (int h = 1; h <= 12 && h * hz < 21000.f; ++h) {
                const float gain = partialGain_[size_t(h - 1)];
                sum += sine(phase_ * h + c * .12f * h * h) * gain;
                weight += gain;
            }
            sum *= .58f / std::max(weight, 1.f);
        } break;
        case 5: { // Fractional-delay Karplus string, variable loss and pick position.
            const float damp = .18f + b * .70f;
            const float coefficient = c * .38f;
            const float filterDelay = (1.f - damp) / damp + (1.f + coefficient) / (1.f - coefficient);
            const float length = std::clamp(sampleRate / hz - filterDelay, 3.f, 4093.f);
            if (pending_) {
                for (auto& v : delay_) v = noise() * (.28f + .30f * b);
                low_ = 0;
            }
            const float v = delayed(length);
            low_ += damp * (v - low_);
            const float dispersive = -coefficient * low_ + allpass_;
            allpass_ = low_ + coefficient * dispersive;
            delay_[size_t(write_)] = dispersive * (.994f + .0057f * a);
            write_ = (write_ + 1) & 4095;
            sum = v * 1.1f;
        } break;
        case 6: { // Nonlinear bowed comb, sustaining friction drives a tuned delay.
            const float length = std::clamp(sampleRate / hz - .5f, 3.f, 4093.f);
            const float v = delayed(length);
            phase_ = wrap(phase_ + step);
            const float velocity = (.12f + .35f * a) * (.75f + .25f * sine(phase_));
            const float delta = velocity - v;
            const float friction = delta / (1.f + std::pow(std::fabs(delta) * (2.f + 10.f * b), 2.f));
            low_ += (.25f + .55f * c) * (v - low_);
            delay_[size_t(write_)] = std::tanh(low_ * .985f + friction * .22f + noise() * .0008f);
            write_ = (write_ + 1) & 4095;
            sum = (v - low_ * .6f) * 2.5f;
        } break;
        case 7: { // Parallel vocal formants on a band-limited pulse source.
            phase_ = wrap(phase_ + step);
            const float input = saw(phase_, step);
            for (int i = 0; i < 3; ++i) {
                const float f = formantF_[size_t(i)];
                auto& low = formantLow_[size_t(i)]; auto& band = formantBand_[size_t(i)];
                low += f * band;
                const float high = input - low - (.11f + .08f * c) * band;
                band += f * high;
                sum += band * (i == 0 ? .18f : .10f);
            }
            sum += sine(phase_) * .10f;
        } break;
        case 8: { // Master/slave hard sync with independently moving pulse width.
            const float previousPhase = phase_;
            phase_ = wrap(phase_ + step);
            const float ratio = 1.f + a * 9.f;
            if (phase_ < previousPhase) phase2_ = wrap(phase_ * ratio);
            else phase2_ = wrap(phase2_ + step * ratio);
            const float inc = std::min(.45f, step * ratio);
            sum = (saw(phase2_, inc) * (1.f - b) + pulse(phase2_, inc, .12f + c * .76f) * b) * .42f;
        } break;
        case 9: { // Smooth wavefolder with symmetry offset, triangle/sine input.
            phase_ = wrap(phase_ + step);
            const float input = triangle(phase_) * (1.f - b) + sine(phase_) * b;
            sum = sine(input * (.5f + 3.5f * a) + (c - .5f) * .5f) * .52f;
        } break;
        case 10: return orbit_.next(step, a, b, c);
        case 11: case 12: case 13: return gravity_.next(step, a, b, c);
        }
        return sum;
    }

    int mode_ = 0, grainIndex_ = 0, write_ = 0;
    bool pending_ = false;
    uint32_t random_ = 0x7b16d9e1u;
    float counter_ = 0, phase_ = 0, phase2_ = 0, phase3_ = 0;
    float previousA_=-99,previousB_=-99,previousC_=-99,density_=-1,spread_=-1,grainGain_=1;
    int noiseOverride_=-1;
    float low_ = 0, allpass_ = 0, last_ = 0, previous_ = 0, dc_ = 0;
    std::array<float, 7> phases_{};
    std::array<float, 3> formantLow_{}, formantBand_{}, formantF_{};
    std::array<float, 12> partialGain_{};
    std::array<float, 1024> table_{};
    std::array<float, 4096> delay_{};
    std::array<Grain, 16> grains_{};
    OctaveOrbit orbit_;
    GravityVoice gravity_;
};
static_assert(sizeof(Voice) < 32768, "Additional oscillator state must remain modest per voice");
}
}
