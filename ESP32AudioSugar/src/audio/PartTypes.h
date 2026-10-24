#pragma once
#include "StaticRack.h"
#include "InstrumentProfile.h"
#include "../instruments/TestInstrument.h"
#include "../instruments/PulseInstrument.h"
#include "../instruments/AcidBass.h"
#include "../instruments/DrumSynth.h"
#if SUGAR_CUSTOM_ENGINE_CHECKS
#include "../instruments/ParameterExample.h"
#endif
namespace SugarAudio {
// Edit these aliases to choose concrete types; no runtime registration required.
#if SUGAR_CUSTOM_ENGINE_CHECKS
using PartOneInstrument=ParameterExample::Instrument;
#else
using PartOneInstrument=AcidBass::Instrument;
#endif
using PartTwoInstrument=DrumSynth::Instrument;
using PartThreeInstrument=TestInstrument::Instrument;
using PartFourInstrument=PulseInstrument::Instrument;
template<unsigned Capacity> struct RackSelection;
template<> struct RackSelection<SugarParts::PART_1+1> { using Type=StaticRack<PartOneInstrument>; };
template<> struct RackSelection<SugarParts::PART_2+1> { using Type=StaticRack<PartOneInstrument,PartTwoInstrument>; };
template<> struct RackSelection<SugarParts::PART_3+1> { using Type=StaticRack<PartOneInstrument,PartTwoInstrument,PartThreeInstrument>; };
template<> struct RackSelection<SugarParts::PART_4+1> { using Type=StaticRack<PartOneInstrument,PartTwoInstrument,PartThreeInstrument,PartFourInstrument>; };
using InstrumentRack=typename RackSelection<SUGAR_PART_CAPACITY>::Type;
static_assert(InstrumentRack::count==SUGAR_PART_CAPACITY,"Rack capacity mismatch");
template<typename Rack> struct RackProfileValidation;
template<typename... Instruments> struct RackProfileValidation<StaticRack<Instruments...>> {
    static constexpr bool valid=(validProfile<Instruments>() && ...);
};
static_assert(RackProfileValidation<InstrumentRack>::valid,"Invalid selected instrument profile");
template<class Rack> struct ParameterLayout;
template<class... Instruments> struct ParameterLayout<StaticRack<Instruments...>> {
    static constexpr unsigned nativeWords=(SugarSynth::nativeWords<Instruments>()+...+0U);
    static constexpr unsigned maxCount=[]() constexpr {
        unsigned result=0;
        for(auto count:{Instruments::parameterCount()...}) if(count>result) result=count;
        return result;
    }();
};
using SynthLayout=ParameterLayout<InstrumentRack>;
static_assert(SynthLayout::nativeWords<65536,"Parameter offsets exceed the event payload");

}
