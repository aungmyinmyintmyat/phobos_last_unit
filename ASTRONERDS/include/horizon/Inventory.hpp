#pragma once
#include "horizon/Types.hpp"
#include <array>
#include <initializer_list>
#include <vector>

namespace horizon {
// Bulk resources use integer grams; cores, plates and sealed fuel kits use counts.
// No floating-point drift can create resources through repeated crafting.
struct ItemStack { Item item; std::int64_t quantity; };
class Inventory {
public:
    std::int64_t count(Item item) const;
    bool add(Item item, std::int64_t quantity);
    bool consume(Item item, std::int64_t quantity);
    bool exchange(std::initializer_list<ItemStack> inputs, std::initializer_list<ItemStack> outputs);
    std::vector<ItemStack> stacks() const;
private:
    std::array<std::int64_t, item_count> quantities_{};
};
}
