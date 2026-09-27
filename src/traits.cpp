#include "traits.h"
#include <algorithm>
#include <cstring>
#include <iterator>

namespace nameplate_lab {
namespace {
struct TraitRow { const char* name; std::uint16_t bits, index; };
struct TraitZone { unsigned nameFirst, nameCount, indexFirst, indexCount; };
#include "trait_data.inc"
}
std::uint16_t LookupTraits(unsigned zone, unsigned index, const char* name) noexcept {
    if(zone>=std::size(Zones))return 0;
    const auto& z=Zones[zone];
    const auto* endIndex=IndexRows+z.indexFirst+z.indexCount;
    const auto* entry=std::lower_bound(IndexRows+z.indexFirst,endIndex,index,
        [](const TraitRow& row,unsigned value){return row.index<value;});
    // Private servers can reuse an index for a different monster.
    if(entry!=endIndex&&entry->index==index&&std::strcmp(entry->name,name)==0)return entry->bits;
    const auto* endName=NameRows+z.nameFirst+z.nameCount;
    entry=std::lower_bound(NameRows+z.nameFirst,endName,name,
        [](const TraitRow& row,const char* value){return std::strcmp(row.name,value)<0;});
    return entry!=endName&&std::strcmp(entry->name,name)==0?entry->bits:0;
}
}
