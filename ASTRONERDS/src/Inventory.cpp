#include "horizon/Inventory.hpp"
#include <limits>
#include <stdexcept>

namespace horizon {
namespace {
std::size_t index_of(Item item) {
    auto i = static_cast<std::size_t>(item);
    if (i >= item_count) throw std::out_of_range("inventory item");
    return i;
}
}
std::int64_t Inventory::count(Item item) const { return quantities_[index_of(item)]; }
bool Inventory::add(Item item, std::int64_t quantity) {
    auto& slot = quantities_[index_of(item)];
    if (quantity < 0 || slot > std::numeric_limits<std::int64_t>::max()-quantity) return false;
    slot += quantity;
    return true;
}
bool Inventory::consume(Item item, std::int64_t quantity) {
    auto& slot = quantities_[index_of(item)];
    if (quantity < 0 || slot < quantity) return false;
    slot -= quantity;
    return true;
}
bool Inventory::exchange(std::initializer_list<ItemStack> inputs, std::initializer_list<ItemStack> outputs) {
    // Work on a copy: every requirement (including repeated items) must succeed.
    // Failed crafts and overflow never partially consume the player's inventory.
    Inventory staged = *this;
    for (auto input : inputs) if (!staged.consume(input.item, input.quantity)) return false;
    for (auto output : outputs) if (!staged.add(output.item, output.quantity)) return false;
    quantities_.swap(staged.quantities_);
    return true;
}
std::vector<ItemStack> Inventory::stacks() const {
    std::vector<ItemStack> result;
    for (std::size_t i = 0; i < item_count; ++i)
        if (quantities_[i] > 0) result.push_back({static_cast<Item>(i), quantities_[i]});
    return result;
}
}
