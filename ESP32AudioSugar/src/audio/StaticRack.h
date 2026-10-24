#pragma once
namespace SugarAudio {
// A tuple-shaped rack with bounded, concrete-type dispatch. No function table,
// heap storage, type erasure, virtual methods or standard tuple machinery.
template<typename... Instruments> class StaticRack;
template<typename Head> class StaticRack<Head> {
public:
    static constexpr unsigned count=1;
    template<typename Function> constexpr bool visit(unsigned index,const Function& function) {
        if(index) return false;
        function(head_); return true;
    }
    template<typename Function> constexpr void forEach(const Function& function) { function(head_); }
private:
    Head head_;
};
template<typename Head,typename... Tail> class StaticRack<Head,Tail...> {
public:
    static constexpr unsigned count=1+sizeof...(Tail);
    template<typename Function> constexpr bool visit(unsigned index,const Function& function) {
        if(!index) { function(head_); return true; }
        return tail_.visit(index-1,function);
    }
    template<typename Function> constexpr void forEach(const Function& function) {
        function(head_); tail_.forEach(function);
    }
private:
    Head head_;
    StaticRack<Tail...> tail_;
};
}
