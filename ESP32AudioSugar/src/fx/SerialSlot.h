#pragma once
#include "InsertCatalog.h"
namespace SugarFX {
class SerialSlot {
    InsertPool pool;
    eAlgorithm active=ALGORITHM_BYPASS,wanted=ALGORITHM_BYPASS;
    Ramp wet;
    constexpr __attribute__((always_inline)) void commit() {
        if(active!=wanted && wet.current==0) {
            active=wanted;
            pool.visit(active,[](auto& effect) { effect.reset(); });
            wet.set(active==ALGORITHM_BYPASS ? 0 : unity);
        }
    }
public:
    constexpr void prepare(uint32_t rate) { pool.prepare(rate); active=wanted=ALGORITHM_BYPASS; wet.reset(0); }
    constexpr __attribute__((always_inline)) bool select(eAlgorithm type) {
        if(type>=ALGORITHMS_COUNT) return false;
        wanted=type;
        if(wanted!=active) wet.set(0);
        else wet.set(active==ALGORITHM_BYPASS ? 0 : unity);
        return true;
    }
    constexpr __attribute__((always_inline)) bool apply(eAlgorithm type,unsigned id,uint32_t value) {
        bool accepted=false;
        pool.visit(type,[&](auto& effect) { accepted=effect.applyControl(id,value); });
        return accepted;
    }
    constexpr __attribute__((always_inline)) void reset() {
        active=wanted;
        pool.visit(active,[](auto& effect) { effect.reset(); });
        wet.reset(active==ALGORITHM_BYPASS ? 0 : unity);
    }
    constexpr __attribute__((always_inline)) void advance(unsigned frames) {
        wet.skip(frames); commit();
        pool.visit(active,[&](auto& effect) __attribute__((always_inline)) { effect.advance(frames); });
    }
    constexpr __attribute__((always_inline)) void processBlock(int32_t* samples,unsigned frames) {
        commit();
        if(active==ALGORITHM_BYPASS) return;
        // Type selection outside the sample loop, even during bypass fades.
        pool.visit(active,[&](auto& effect) __attribute__((always_inline)) {
            for(unsigned f=0;f<frames;++f) {
                const unsigned i=f*2;
                const auto mix=wet.step();
                const Frame dry{samples[i],samples[i+1]},out=effect.process(dry);
                samples[i]=dry.left+scale(out.left-dry.left,mix);
                samples[i+1]=dry.right+scale(out.right-dry.right,mix);
            }
        });
    }
};
}
