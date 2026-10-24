#include "ControlLogic.h"
namespace {
using namespace SugarControl;
constexpr bool buttons() {
    Button b;
    if(b.update(true,0)||b.update(false,5)||b.update(true,8)||b.update(true,27)) return false;
    if(!b.update(true,28)||!b.pressed||b.update(true,100)) return false;
    return !b.update(false,101)&&b.update(false,121)&&!b.pressed;
}
constexpr bool encoder() {
    Encoder e;
    if(e.update(1)||e.update(0)||e.update(2)||e.update(3)!=1) return false;
    if(e.update(2)||e.update(0)||e.update(1)||e.update(3)!=-1) return false;
    if(e.update(1)||e.update(3)) return false;
    return !e.update(0)&&!e.update(3);
}
constexpr bool pages() {
    if(menuItemCount(0) || menuItemCount(SugarParts::PARTS_COUNT+1)) return false;
    for(unsigned capacity=1;capacity<=SugarParts::PARTS_COUNT;++capacity) {
        const auto count=menuItemCount(capacity);
        if(menuPage(MENU_MIDI,capacity)!=PAGE_ROUTING || menuPage(MENU_FX,capacity)!=PAGE_FX ||
            menuPage(count-1,capacity)!=PAGE_PRESETS || menuPage(count,capacity)!=PAGES_COUNT) return false;
        for(unsigned p=0;p<capacity;++p) if(menuPage(MENU_SYNTH_FIRST+p,capacity)!=PAGE_SYNTH) return false;
        if(wrap(0,-1,count)!=count-1 || wrap(count-1,1,count)!=0) return false;
    }
    return true;
}
static_assert(pages(),"Home pages match capacity, synth indices and wrap bounds");
constexpr bool routing() {
    using namespace SugarParts;
    if(routingFieldCount(0)||routingFieldCount(PARTS_COUNT+1)) return false;
    for(unsigned capacity=1;capacity<=PARTS_COUNT;++capacity) {
        const auto count=routingFieldCount(capacity);
        if(routingAction(0,capacity)!=ROUTING_FIELD_CHANNEL ||
            routingAction(count-1,capacity)!=ROUTING_FIELD_BACK ||
            routingAction(count,capacity)!=ROUTING_FIELDS_COUNT ||
            routingPart(0,capacity)!=PARTS_COUNT || wrap(0,-1,count)!=count-1) return false;
        uint32_t mask=0;
        for(unsigned p=0;p<capacity;++p) {
            const auto id=ePartId(p);
            if(routingAction(p+1,capacity)!=ROUTING_FIELD_PART || routingPart(p+1,capacity)!=id) return false;
            mask=editedRoute(mask,id,true);
            if(!(mask&partBit(id)) || editedRoute(mask,id,true)!=mask) return false;
        }
        for(unsigned p=0;p<capacity;++p) {
            const auto id=ePartId(p);
            const auto next=editedRoute(mask,id,false);
            if((next&partBit(id)) || ((mask^next)!=partBit(id))) return false;
            mask=next;
        }
        if(mask) return false;
    }
    return parameterWindow(FIELD_PARAMETER_FIRST+2,3)==0 && parameterWindow(FIELD_PARAMETER_FIRST+3,3)==1;
}
static_assert(routing(),"Routing fields, mute/layer masks, inactive-independent edits and parameter scrolling");
static_assert(buttons(),"Debounce press/release without repeat");
static_assert(encoder(),"Quadrature cycles, reversal and invalid transitions");
static_assert(wrap(0,-1,2)==1 && wrap(1,1,2)==0 && adjusted(99,1,10,0,100)==100 &&
    adjusted(0,-1,1,0,127)==0,"Navigation wrap and bounded edits");
constexpr bool presentation() {
    for(int32_t p=0;p<=100;++p) if(percent(gainFromPercent(p))!=p) return false;
    return doubleGlyphHalf(15,0)==255 && doubleGlyphHalf(15,1)==0 &&
        doubleGlyphHalf(0x70,1)==63 && doubleGlyphHalf(0,0)==0 &&
        percent(0)==0 && percent(127)==100 && gainFromPercent(100)==127 &&
        rangePosition(INT32_MIN,INT32_MIN,INT32_MAX,19)==0 &&
        rangePosition(INT32_MAX,INT32_MIN,INT32_MAX,19)==19 &&
        rangePosition(0,0,0,19)==0 && rangePosition(50,0,100,20)==10;
}
static_assert(presentation(),"Percent editing round trips and wide signed range display must be safe");

constexpr bool spacedText() {
    // Every seven-pixel glyph must survive splitting across eight-pixel OLED pages.
    for(unsigned row=0;row<textBodyRows;++row) for(unsigned glyph=0;glyph<128;++glyph) {
        uint64_t reconstructed=0;
        for(unsigned page=0;page<8;++page)
            reconstructed|=uint64_t(placeGlyph(uint8_t(glyph),int(row*textLinePitch)-int(page*8)))<<(page*8);
        if(reconstructed!=(uint64_t(glyph)<<(row*textLinePitch))) return false;
    }
    return placeGlyph(127,-7)==0 && placeGlyph(127,8)==0 &&
        uint64_t(placeGlyph(127,1))<<(7*8)==uint64_t(127)<<textFooterY;
}
static_assert(spacedText(),"Spaced text must cross OLED byte pages without losing glyph rows or leading gaps");

static_assert(selectionPadding(1)==1 && selectionPadding(0)==128 &&
    selectionPadding(-7)==1 && selectionPadding(8)==128,
    "Selection margins must cross OLED pages and clip at display edges");

}
