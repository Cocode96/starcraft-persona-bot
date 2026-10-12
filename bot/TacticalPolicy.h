#pragma once
#include <algorithm>

// Shared execution safeguards, not learned personal micro or a strength model.
class TacticalPolicy
{
public:
    // Clearance is distance outside visible ground-weapon range, in pixels.
    // Reject paths that deepen contact even when their endpoint looks safe.
    static double casterPositionScore(int clearance, int pathClearance, int currentClearance, int travel,
        int regroupDistance = 640, int edgeDistance = 128)
    {
        if (pathClearance < std::min(0, currentClearance) - 16) return -100000;
        const double regroup = clearance >= 96 ? std::max(0, 640 - regroupDistance) * 0.1 : 0;
        const double room = clearance >= 96 ? std::min(128, edgeDistance) * 0.25 : 0;
        return std::min(320, clearance) - travel * 0.35 + regroup + room;
    }

    enum class Stance { Gather, Advance, Defend, Hold, Retreat };
    struct Situation
    {
        int frame = 0;
        int attackFrame = 0;
        int friendlySupply = 0; // BWAPI half-supply units, combat troops only.
        double nearbyFriendlyPower = 0;
        double nearbyEnemyPower = 0;
        bool baseThreatened = false;
        bool enemyTemplarSeen = false;
        bool enemyBaseRecentlySeen = false;
        bool hasLurkers = false;
        int retreatUntil = 0;
    };

    static Stance choose(const Situation& s)
    {
        if (s.baseThreatened) return Stance::Defend;
        if (s.frame < s.retreatUntil) return Stance::Retreat;
        if (s.nearbyEnemyPower > 0 && s.nearbyFriendlyPower < s.nearbyEnemyPower * 0.72)
            return Stance::Retreat;
        if (s.frame < s.attackFrame || s.friendlySupply < 16) return Stance::Gather;
        if (s.hasLurkers && s.enemyTemplarSeen && !s.enemyBaseRecentlySeen &&
            s.nearbyFriendlyPower < s.nearbyEnemyPower * 1.6) return Stance::Hold;
        return Stance::Advance;
    }

    static const char* name(Stance stance)
    {
        switch (stance)
        {
        case Stance::Advance: return "advance";
        case Stance::Defend: return "defend";
        case Stance::Hold: return "hold";
        case Stance::Retreat: return "retreat";
        default: return "gather";
        }
    }
};
