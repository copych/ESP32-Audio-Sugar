#pragma once
#include <stdint.h>
#include "../parts/PartRouting.h"
namespace SugarControl {
enum eButton : uint8_t { BUTTON_BACK, BUTTON_NEXT_PAGE, BUTTON_NEXT_FIELD,
    BUTTON_AUDITION, BUTTON_ENCODER, BUTTONS_COUNT };
enum eField : uint8_t { FIELD_HOME, FIELD_CONTEXT, FIELD_PARAMETER_FIRST,
    FIELDS_FIXED_COUNT = FIELD_PARAMETER_FIRST };
enum eInteraction : uint8_t { INTERACTION_BROWSE, INTERACTION_EDIT, INTERACTIONS_COUNT };
enum ePage : uint8_t { PAGE_SYNTH, PAGE_PRESETS, PAGE_ROUTING, PAGE_MENU, PAGE_FX, PAGES_COUNT };
enum eMenuItem : uint8_t { MENU_MIDI, MENU_FX, MENU_SYNTH_FIRST,
    MENU_PRESETS=MENU_SYNTH_FIRST+SugarParts::PARTS_COUNT, MENU_ITEMS_COUNT };
constexpr unsigned menuItemCount(unsigned capacity) {
    return capacity && capacity<=SugarParts::PARTS_COUNT ? MENU_SYNTH_FIRST+capacity+1 : 0;
}
constexpr ePage menuPage(unsigned item,unsigned capacity) {
    const unsigned count=menuItemCount(capacity);
    return item>=count ? PAGES_COUNT : item==MENU_MIDI ? PAGE_ROUTING : item==MENU_FX ? PAGE_FX :
        item+1==count ? PAGE_PRESETS : PAGE_SYNTH;
}
enum eRoutingField : uint8_t { ROUTING_FIELD_CHANNEL, ROUTING_FIELD_PART, ROUTING_FIELD_BACK, ROUTING_FIELDS_COUNT };
constexpr unsigned routingFieldCount(unsigned capacity) {
    return capacity && capacity<=SugarParts::PARTS_COUNT ? capacity+unsigned(ROUTING_FIELDS_COUNT)-1 : 0;
}
constexpr eRoutingField routingAction(unsigned field,unsigned capacity) {
    const unsigned count=routingFieldCount(capacity);
    return field>=count ? ROUTING_FIELDS_COUNT : field==ROUTING_FIELD_CHANNEL ? ROUTING_FIELD_CHANNEL :
        field+1==count ? ROUTING_FIELD_BACK : ROUTING_FIELD_PART;
}
constexpr SugarParts::ePartId routingPart(unsigned field,unsigned capacity) {
    return routingAction(field,capacity)==ROUTING_FIELD_PART ?
        SugarParts::ePartId(field-unsigned(ROUTING_FIELD_PART)) : SugarParts::PARTS_COUNT;
}
constexpr uint32_t editedRoute(uint32_t mask,SugarParts::ePartId part,bool enabled) {
    return enabled ? mask|SugarParts::partBit(part) : mask&~SugarParts::partBit(part);
}
enum ePresetField : uint8_t { PRESET_FIELD_STORAGE, PRESET_FIELD_SLOT, PRESET_FIELD_SAVE, PRESET_FIELD_LOAD,
    PRESET_FIELD_MEDIA_ACTION, PRESET_FIELD_BACK, PRESET_FIELDS_COUNT };
struct Button {
    bool candidate=false, pressed=false;
    uint32_t since=0;
    constexpr bool update(bool down,uint32_t now) {
        if(down!=candidate) { candidate=down; since=now; }
        if(candidate!=pressed && uint32_t(now-since)>=20) { pressed=candidate; return true; }
        return false;
    }
};
struct Encoder {
    uint8_t previous=3;
    int8_t accumulation=0;
    constexpr int update(uint8_t state) {
        // Invalid two-bit jumps reset partial movement; reversals cancel bounce.
        if((state^previous)==3) accumulation=0;
        else if(state!=previous) {
            const bool forward=(previous==3 && state==1)||(previous==1 && state==0)||
                (previous==0 && state==2)||(previous==2 && state==3);
            accumulation+=forward ? 1 : -1;
        }
        previous=state;
        if(state!=3) return 0;
        const int result=accumulation>=4 ? 1 : accumulation<=-4 ? -1 : 0;
        accumulation=0;
        return result;
    }
};
constexpr unsigned wrap(unsigned value,int delta,unsigned count) {
    return !count ? 0 : delta>0 ? (value+1)%count : delta<0 ? (value+count-1)%count : value%count;
}
constexpr int32_t adjusted(int32_t value,int delta,int32_t step,int32_t low,int32_t high) {
    const int64_t next=int64_t(value)+int64_t(delta)*step;
    return next<low ? low : next>high ? high : int32_t(next);
}
// Presentation conversions stay on Core1; stored/queued gains remain 0..127.
constexpr int32_t percent(int32_t value) { return (value*100+63)/127; }
constexpr int32_t gainFromPercent(int32_t value) { return (value*127+50)/100; }
constexpr unsigned textLinePitch=9,textBodyRows=6,textFooterY=57;
constexpr uint8_t placeGlyph(uint8_t glyph,int offset) {
    return offset<=-7 || offset>=8 ? 0 : offset<0 ? uint8_t(glyph>>(-offset)) : uint8_t(glyph<<offset);
}
constexpr uint8_t selectionPadding(int offset) {
    return placeGlyph(1,offset-1)|placeGlyph(1,offset+7);
}
constexpr uint8_t doubleGlyphHalf(uint8_t source,unsigned half) {
    const uint8_t nibble=(source>>(half ? 4 : 0))&15;
    return uint8_t((nibble&1)*3 | (nibble&2)*6 | (nibble&4)*12 | (nibble&8)*24);
}
constexpr unsigned rangePosition(int32_t value,int32_t low,int32_t high,unsigned width) {
    return high<=low || value<=low ? 0 : value>=high ? width :
        unsigned((uint64_t(int64_t(value)-low)*width)/uint64_t(int64_t(high)-low));
}
constexpr unsigned parameterWindow(unsigned field,unsigned visible) {
    return visible && field>=FIELD_PARAMETER_FIRST+visible ? field-FIELD_PARAMETER_FIRST-visible+1 : 0;
}
}
