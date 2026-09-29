#include "traits.h"
#include <algorithm>
#include <cstring>
#include <iterator>

namespace overhead {
namespace {
struct TraitRow { const char* name; std::uint16_t bits, index, weak, resist; };
struct TraitZone { unsigned nameFirst, nameCount, indexFirst, indexCount; };
#include "trait_data.inc"
}
MonsterTraits LookupMonster(unsigned zone, unsigned index, const char* name) noexcept {
    if(zone>=std::size(Zones))return {};
    const auto& z=Zones[zone];
    const auto* endIndex=IndexRows+z.indexFirst+z.indexCount;
    const auto* entry=std::lower_bound(IndexRows+z.indexFirst,endIndex,index,
        [](const TraitRow& row,unsigned value){return row.index<value;});
    // Private servers can reuse an index for a different monster.
    if(entry!=endIndex&&entry->index==index&&std::strcmp(entry->name,name)==0)return {entry->bits,entry->weak,entry->resist};
    const auto* endName=NameRows+z.nameFirst+z.nameCount;
    entry=std::lower_bound(NameRows+z.nameFirst,endName,name,
        [](const TraitRow& row,const char* value){return std::strcmp(row.name,value)<0;});
    if(entry!=endName&&std::strcmp(entry->name,name)==0)return {entry->bits,entry->weak,entry->resist};
    return {};
}
}
