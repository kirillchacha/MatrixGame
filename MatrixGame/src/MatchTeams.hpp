#pragma once

#include <array>
#include <span>

// Side zero stays neutral. Ownership and control still use the original side id.
struct CMatchTeams {
    std::array<int, 5> teams{0, 1, 2, 3, 4};

    constexpr bool Allied(int a, int b) const {
        return a == b || (a > 0 && a < 5 && b > 0 && b < 5 && teams[a] == teams[b]);
    }

    constexpr bool HasOpponent(std::span<const int> sides, int player) const {
        for (int side : sides)
            if (!Allied(side, player))
                return true;
        return false;
    }

    enum class Outcome { Ongoing, Won, Lost };

    constexpr Outcome Result(std::span<const bool> alive, int player) const {
        bool ally = false;
        bool enemy = false;
        for (int id = 1; id < (int)alive.size(); ++id) {
            if (!alive[id])
                continue;
            if (Allied(id, player))
                ally = true;
            else
                enemy = true;
        }
        return !ally ? Outcome::Lost : enemy ? Outcome::Ongoing : Outcome::Won;
    }
};

inline CMatchTeams g_MatchTeams;
inline bool SidesAllied(int a, int b) { return g_MatchTeams.Allied(a, b); }
