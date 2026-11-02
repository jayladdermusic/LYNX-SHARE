// Round 168l (YouTube review of 0.1.21, 5:33–5:58: "this monophonic is not turning on. So I really can't play legato
// stuff"): Channel settings › Misc › Mono / Porta / Slide on every generator, played by the rack's channel layer as FL's
// channel voice code does (REA_rodada168l, core/ChannelPoly.h)

#include "core/ChannelPoly.h"
#include "core/HostSlide.h"

namespace mono168l
{
// an instrument that records what the rack asks of it (a hosted plugin's place in the rack)
struct VoiceSpy final : lynx::Instrument
{
    struct Got
    {
        char what = 'n'; // n note on, f note off, s slide
        int key = 0;
        float cents = 0.0f, glideFrom = 0.0f, glideSamples = 0.0f, samples = 0.0f;
    };
    std::vector<Got> got;
    float nextCents = 0.0f;
    void prepare (double) noexcept override {}
    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void noteOn (int key, float) noexcept override
    {
        auto& g = lynx::HostSlide::pendingGlide();
        got.push_back ({ 'n', key, nextCents, g.fromCents, g.samples, 0.0f });
        g.taken = g.samples > 0.0f;
        nextCents = 0.0f;
    }
    void noteOff (int key) noexcept override { got.push_back ({ 'f', key }); }
    void slideTo (int key, float cents, float samples) noexcept override { got.push_back ({ 's', key, cents, 0.0f, 0.0f, samples }); }
    bool followsChannelPorta() const noexcept override { return true; }
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    std::string trace() const
    {
        std::string s;
        for (const auto& g : got)
            s += std::string (1, g.what) + std::to_string (g.key) + " ";
        return s;
    }
};

struct Rig
{
    lynx::Song song;
    std::shared_ptr<VoiceSpy> spy = std::make_shared<VoiceSpy>();
    std::unique_ptr<const lynx::RackState> state;
    lynx::RackRenderer renderer;
    lynx::LiveEventQueue live;
    lynx::RackState::Factories f;

    explicit Rig (std::function<void (lynx::ChannelDef&)> setup)
    {
        song.mixer = lynx::Song::defaultMixer (2);
        song.bpm = 120.0;
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::plugin;
        c.plugin.id = "spy";
        setup (c);
        song.channels.push_back (c);
        song.patterns.emplace_back();
        f.pluginInstruments = [this] (const lynx::PluginRef&, double) { return std::shared_ptr<lynx::Instrument> (spy); };
        state = lynx::RackState::build (song, nullptr, 48000.0, f);
        renderer.prepare (48000.0, 256);
    }
    void play (int key, bool on = true, int colour = 0)
    {
        lynx::NoteEvent e;
        e.type = on ? lynx::NoteEvent::Type::on : lynx::NoteEvent::Type::off;
        e.key = key;
        e.velocity = 0.8f;
        e.colour = (std::int8_t) colour;
        live.push (e);
        std::vector<float> l (256), r (256);
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), &live, nullptr);
    }
};
} // namespace mono168l

TEST (mono168l_misc_mono_porta_slide_on_every_generator)
{
    using namespace lynx;
    // the video's channels: an imported 3x Osc, LYNX's built-in Kick / Hat, Toxic Biohazard, a Sampler, a hosted plugin
    for (const auto kind : { InstrumentKind::threeosc, InstrumentKind::kick, InstrumentKind::hat, InstrumentKind::toxic, InstrumentKind::sampler,
                             InstrumentKind::plugin, InstrumentKind::sytrus, InstrumentKind::soundfont, InstrumentKind::patcher })
    {
        ChannelDef c;
        c.instrument = kind;
        CHECK (channelHasMono (c) && channelHasPortamento (c)); // (main: only the LYNX Synth / Sampler had them)
        setChannelMono (c, true);
        setChannelPortamento (c, true, 0.5f);
        CHECK (channelMono (c) && channelPortamento (c) && std::abs (channelPortamentoBeats (c) - 0.5f) < 1e-6f);
        setChannelMono (c, false);
        CHECK (! channelMono (c) && channelPortamento (c));
    }
    // the LYNX Synth keeps its own flags (its page and the Misc tab show the same)
    ChannelDef synth;
    setChannelMono (synth, true);
    setChannelPortamento (synth, true, 9.0f); // (clamped to 4 beats)
    CHECK (synth.synth.mono && synth.synth.porta && std::abs (synth.synth.portaBeats - 4.0f) < 1e-6f && channelMono (synth));
    // channels with no sound of their own have nothing to make mono
    for (const auto kind : { InstrumentKind::controller, InstrumentKind::midiOut, InstrumentKind::layer, InstrumentKind::vfx, InstrumentKind::audioClip })
    {
        ChannelDef c;
        c.instrument = kind;
        setChannelMono (c, true);
        CHECK (! channelHasMono (c) && ! channelMono (c));
    }
}

TEST (mono168l_fl_glide_time)
{
    using namespace lynx::channelpoly;
    // FL's Slide knob: n = round((27^(v/1024) − 1)·48/26) 1/192 beats; FL's porta ticks n·24/48 at 96 PPQ; the glide
    // lasts ticks + 1 ticks (FLEngine 0x1312200, 0xd26460)
    CHECK (portaTicks (24.0f / 192.0f) == 12 && std::abs (glideBeats (24.0f / 192.0f) - 13.0 / 96.0) < 1e-12); // 820, FL's default
    CHECK (portaTicks (384.0f / 192.0f) == 192 && std::abs (glideBeats (2.0f) - 193.0 / 96.0) < 1e-12);       // 1660, the maximum
    CHECK (portaTicks (1.0f / 192.0f) == 0 && std::abs (glideBeats (1.0f / 192.0f) - 1.0 / 96.0) < 1e-12);    // ~0: no porta glide
    CHECK (portaTicks (7.0f / 192.0f) == 3);                                                                  // 500, old files
}

TEST (mono168l_channel_layer_mono_and_porta_like_fl)
{
    using namespace lynx::channelpoly;
    Layer layer;
    Mode mono;
    mono.mono = true;
    // first note: a voice
    auto on = layer.noteOn (mono, 60, 0.0f, false, 0);
    CHECK (on.start && ! on.slide && ! on.glide && on.release < 0);
    // played over it: no voice, the sounding one glides there (FL's virtual voice, 0xd236d0 → 0xfeaf70)
    on = layer.noteOn (mono, 67, 0.0f, false, 0);
    CHECK (! on.start && on.slide);
    on = layer.noteOn (mono, 72, 0.0f, false, 0);
    CHECK (! on.start && on.slide);
    // the newest released: back to the newest still held (67); then 67 released: back to 60
    auto off = layer.noteOff (mono, 72, 0);
    CHECK (off.slide && off.key == 67 && off.release < 0);
    off = layer.noteOff (mono, 67, 0);
    CHECK (off.slide && off.key == 60 && off.release < 0);
    // the last held note ends the voice, whatever its key (the voice keeps its first key)
    layer.noteOn (mono, 64, 0.0f, false, 0);
    off = layer.noteOff (mono, 60, 0);
    CHECK (! off.slide || off.key == 64);
    off = layer.noteOff (mono, 64, 0);
    CHECK (off.release == 60 && ! off.slide);
    // not overlapping: a new voice
    on = layer.noteOn (mono, 62, 0.0f, false, 0);
    CHECK (on.start && ! on.slide);
    // another colour is another group (FL: one mono voice per note colour / MIDI channel)
    on = layer.noteOn (mono, 50, 0.0f, false, 3);
    CHECK (on.start && ! on.slide);
    CHECK (layer.noteOff (mono, 50, 3).release == 50 && layer.noteOff (mono, 62, 0).release == 62);
    // an instrument that cannot slide: the new note takes the voice over
    Mode drums = mono;
    drums.slides = false;
    layer.noteOn (drums, 36, 0.0f, false, 0);
    on = layer.noteOn (drums, 38, 0.0f, false, 0);
    CHECK (on.start && on.release == 36);
    CHECK (layer.noteOff (drums, 36, 0).release < 0 && layer.noteOff (drums, 38, 0).release == 38);
    // with the arpeggiator on FL keeps no mono list (0xd28910)
    Mode arp = mono;
    arp.arpeggiator = true;
    layer.noteOn (arp, 60, 0.0f, false, 0);
    CHECK (layer.noteOn (arp, 64, 0.0f, false, 0).start);
    layer.reset();

    // Porta: from the group's last note (even ended), linear in cents; a note's porta flag inverts the channel's
    Mode porta;
    porta.porta = true;
    CHECK (! layer.noteOn (porta, 60, 0.0f, false, 0).glide); // no note before
    layer.noteOff (porta, 60, 0);
    on = layer.noteOn (porta, 67, 25.0f, false, 0);
    CHECK (on.start && on.glide && std::abs (on.glideFrom - (6000.0f - 6725.0f)) < 1e-3f);
    CHECK (! layer.noteOn (porta, 72, 0.0f, true, 0).glide); // porta note on a Porta channel: no glide
    Mode plain;
    on = layer.noteOn (plain, 60, 0.0f, true, 0); // porta note on a channel without Porta: glide
    CHECK (on.glide && std::abs (on.glideFrom - 1200.0f) < 1e-3f);
    // no glide with a Slide under one tick, the echo delay's feedback, the arpeggiator, or an engine that can't
    {
        auto tiny = porta, echo = porta, arpeggio = porta, engine = porta;
        tiny.portaBeats = 1.0f / 192.0f;
        echo.echo = true;
        arpeggio.arpeggiator = true;
        engine.glidesIn = false;
        int key = 50;
        for (const auto& m : { tiny, echo, arpeggio, engine })
            CHECK (! layer.noteOn (m, ++key, 0.0f, false, 0).glide);
    }
    // Mono + Porta, not overlapping: the new voice glides in from the last note
    layer.reset();
    Mode both = mono;
    both.porta = true;
    layer.noteOn (both, 60, 0.0f, false, 0);
    layer.noteOff (both, 60, 0);
    on = layer.noteOn (both, 64, 0.0f, false, 0);
    CHECK (on.start && on.glide && std::abs (on.glideFrom + 400.0f) < 1e-3f);
}

