#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Which sides a map lets a human take over.
//
// A side joins the match only when the map gives it a base or at least one robot: those are the
// two things CMatrixMap::StaticPrepare2() turns into SS_ACTIVE. Owning nothing but a factory, a
// turret or cannons leaves the side inactive, and CMatrixMapLogic::Takt() then ends the match at
// once — with only one active side left it declares the player the winner. Neutral objects
// (owner 0) and base ruins (owner 255) belong to nobody.
class CPlayableSides {
public:
    // Of the buildings only bases count.
    void AddBuildings(std::span<const uint8_t> owners, std::span<const uint8_t> kinds, uint8_t base_kind) {
        if (owners.size() != kinds.size())
            return;  // malformed map data: trust neither array
        for (size_t i = 0; i < owners.size(); ++i)
            if (kinds[i] == base_kind)
                Mark(owners[i]);
    }

    // Every robot keeps its side alive, even without a base of its own.
    void AddRobots(std::span<const uint8_t> owners) {
        for (uint8_t owner : owners)
            Mark(owner);
    }

    std::vector<int> Result(void) const {
        std::vector<int> result;
        for (int id = 1; id <= 4; ++id)
            if (m_Present[id])
                result.push_back(id);
        return result;
    }

private:
    void Mark(uint8_t owner) {
        if (owner >= 1 && owner <= 4)
            m_Present[owner] = true;
    }

    bool m_Present[5]{};
};
