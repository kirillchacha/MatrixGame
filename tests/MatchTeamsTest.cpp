#include "../MatrixGame/src/MatchTeams.hpp"

constexpr bool CheckMatchTeams() {
    CMatchTeams ffa;
    for (int a = 0; a <= 5; ++a)
        for (int b = 0; b <= 5; ++b)
            if (ffa.Allied(a, b) != (a == b))
                return false;

    CMatchTeams teams{{0, 1, 2, 1, 2}};
    if (!teams.Allied(1, 3) || !teams.Allied(3, 1) || !teams.Allied(2, 4) || teams.Allied(1, 2) ||
        teams.Allied(0, 1) || teams.Allied(-1, 1) || teams.Allied(5, 1))
        return false;

    const int allies[] = {1, 3};
    const int opponents[] = {2, 3};
    if (teams.HasOpponent(allies, 1) || !teams.HasOpponent(opponents, 3))
        return false;

    using Outcome = CMatchTeams::Outcome;
    // Player eliminated, ally continues; neutral presence does not prevent victory.
    const bool fighting[] = {true, false, true, true, false};
    const bool won[] = {true, false, false, true, false};
    const bool lost[] = {false, false, true, false, true};
    const bool empty[] = {true, false, false, false, false};
    return teams.Result(fighting, 1) == Outcome::Ongoing && teams.Result(won, 1) == Outcome::Won &&
           teams.Result(lost, 1) == Outcome::Lost && teams.Result(empty, 1) == Outcome::Lost &&
           ffa.Result(fighting, 1) == Outcome::Lost;
}

static_assert(CheckMatchTeams());
