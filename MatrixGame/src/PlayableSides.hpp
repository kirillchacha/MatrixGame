#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Only a real, owned robot base grants a starting slot. Neutral buildings,
// factories, ruins (owner 255), and robots alone never grant a slot.
inline std::vector<int> CollectPlayableSides(std::span<const uint8_t> owners,
                                              std::span<const uint8_t> kinds, uint8_t base_kind) {
    if (owners.size() != kinds.size())
        return {};
    bool present[5]{};
    for (size_t i = 0; i < owners.size(); ++i) {
        if (owners[i] >= 1 && owners[i] <= 4 && kinds[i] == base_kind)
            present[owners[i]] = true;
    }
    std::vector<int> result;
    for (int id = 1; id <= 4; ++id)
        if (present[id])
            result.push_back(id);
    return result;
}
