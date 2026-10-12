#include "TacticalPolicy.h"
#include <iostream>
#include <stdexcept>

using Stance = TacticalPolicy::Stance;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
int main()
{
    try
    {
        TacticalPolicy::Situation s;
        s.frame = 10000; s.attackFrame = 6000; s.friendlySupply = 8;
        require(TacticalPolicy::choose(s) == Stance::Gather, "Four lings cannot trigger endless pushes");
        s.friendlySupply = 60; s.nearbyFriendlyPower = 30; s.nearbyEnemyPower = 60;
        require(TacticalPolicy::choose(s) == Stance::Retreat, "Losing local fight must disengage");
        s.nearbyFriendlyPower = 65; s.retreatUntil = 10240;
        require(TacticalPolicy::choose(s) == Stance::Retreat, "Retreat hysteresis missing");
        s.baseThreatened = true;
        require(TacticalPolicy::choose(s) == Stance::Defend, "Base threat must override normal retreat");
        s.baseThreatened = false; s.retreatUntil = 0; s.hasLurkers = true; s.enemyTemplarSeen = true;
        require(TacticalPolicy::choose(s) == Stance::Hold, "Uncertain templar contact should hold lurker position");
        s.enemyBaseRecentlySeen = true;
        require(TacticalPolicy::choose(s) == Stance::Advance, "New scouting evidence should allow advance");
        require(TacticalPolicy::casterPositionScore(160, -40, -40, 160) >
                TacticalPolicy::casterPositionScore(-40, -40, -40, 0), "Caster should escape current weapon range");
        require(TacticalPolicy::casterPositionScore(320, -80, 40, 160) <
                TacticalPolicy::casterPositionScore(40, 40, 40, 0), "Safe endpoint cannot justify crossing enemy fire");
        require(TacticalPolicy::casterPositionScore(320, 320, 320, 0) >
                TacticalPolicy::casterPositionScore(500, 320, 320, 160), "Safe caster should not flee indefinitely");
        std::cout << "Tactical execution safeguards validated\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
