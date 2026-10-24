#include "../audio/MixMath.h"
#include <limits.h>
namespace {
constexpr bool clipBounds() {
    using namespace SugarMix;
    int32_t previous=0;
    for(int32_t value=0;value<=knee+int32_t(width)+1;++value) {
        const auto shaped=softClip(value);
        if(shaped<previous || softClip(-value)!=-shaped) return false;
        if(value<=knee && shaped!=value) return false;
        previous=shaped;
    }
    return softClip(INT_MAX)==32767 && softClip(INT_MIN)==-32767 && softClip(0)==0;
}
constexpr bool summation() {
    using namespace SugarMix;
    return gainQ15<1>()==32768 && gainQ15<2>()==23170 && gainQ15<4>()==16384 &&
        normalize<1>(32767)==32767 && normalize<2>(2*32767)>32767 &&
        softClip(normalize<2>(2*32767))<32767 && softClip(normalize<2>(2*32767))>knee &&
        softClip(normalize<4>(4*-32768))==-32767 && normalize<2>(-12345)==-normalize<2>(12345) &&
        gainQ15<8>()>32768/8 && gainQ15<5>()>32768/5;
}
static_assert(clipBounds(),"Soft shoulder must be monotonic, odd, bounded and unity below knee");
static_assert(summation(),"Equal-power normalization must retain peak protection for correlated voices");
}
