#include <cstdio>
#include "core/BankStates.h"
#include "core/DrumControls.h"
#include "core/Drumaxx.h"
#include "core/DrumSynthLive.h"
#include "core/FruitKick.h"
#include "core/Plucked.h"
using namespace lynx;
#define P(x) std::printf ("%d %s\n", __LINE__, #x), std::fflush (stdout)
int main()
{
    P(a); auto kick = bankstates::channelFor ("fruitkick", { 100, -500, 70, 130, 20, 5 });
    P(b); const auto* minFreq = drumcontrols::find (InstrumentKind::fruitkick, "Min freq");
    P(c); auto v = drumcontrols::value (*kick, *minFreq);
    P(d); drumcontrols::setValue (*kick, *minFreq, -5000.0f);
    P(e); ChannelDef empty; empty.instrument = InstrumentKind::plucked;
    P(f); auto* dec = drumcontrols::find (InstrumentKind::plucked, "Decay"); std::printf ("%p\n", (void*) dec);
    P(g); drumcontrols::value (empty, *dec);
    P(h); drumcontrols::setValue (empty, *drumcontrols::find (InstrumentKind::plucked, "Gate"), 1.0f);
    P(i); auto pluck = bankstates::channelFor ("plucked", { 200, 30, 1, 0, 1 });
    P(j); std::vector<bankstates::DrumaxxPad> pads (3); pads[2].voice.assign (38, 0.25f);
    ChannelDef maxx; maxx.instrument = InstrumentKind::drumaxx;
    P(k); maxx.pluginState = bankstates::drumaxxState ("LYNX test", pads);
    P(l); drumaxxStateUsable (maxx.pluginState);
    P(m); const auto* pitch = drumcontrols::find (InstrumentKind::drumaxx, "Pitch"); std::printf ("%p\n", (void*) pitch);
    P(n); drumcontrols::value (maxx, *pitch, 2);
    P(o); drumcontrols::setValue (maxx, *pitch, 0.4f, 1);
    P(p); auto* pv = drumcontrols::find (InstrumentKind::drumaxx, "Pad 3 volume"); std::printf ("%p\n", (void*) pv);
    P(q); drumcontrols::value (maxx, *pv);
    P(r); std::vector<bankstates::DrumSynthPatch> patches (1); patches[0].key = 50; patches[0].values.assign (38, 10);
    ChannelDef dsl; dsl.instrument = InstrumentKind::drumsynth;
    P(s); dsl.modelParams = bankstates::drumSynthParams (patches);
    P(t); drumcontrols::defaultPart (dsl);
    P(u); const auto* level = drumcontrols::find (InstrumentKind::drumsynth, "Level");
    P(v); drumcontrols::value (dsl, *level, 50);
    P(w); drumcontrols::setValue (dsl, *level, 200.0f, 51);
    P(z);
}
