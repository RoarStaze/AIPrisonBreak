#include "world.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <string>
using namespace prison;
int main(){
    World w;
    assert(w.solid(0,0) && !w.solid(5,5));
    assert(w.solid(10,6));
    assert(!w.lineOfSight({5.5f,6.5f},{14.5f,6.5f}));
    assert(!w.command("dance").ok);
    assert(!w.command("interact","security_terminal").ok);
    assert(!w.command("move","",{0,0}).ok);
    assert(!w.command("alarm","99").ok);
    assert(w.command("unlock","cell_door").ok);
    assert(w.command("open","cell_door").ok);
    assert(!w.solid(10,6));
    assert(w.command("cameras","off").ok&&!w.cameras);
    assert(w.command("lights","off").ok&&!w.lights);
    assert(w.command("alarm","3").ok&&w.lockdown&&w.alert==3);
    assert(w.command("speak","Stop right there.").ok);
    assert(w.guardSpeech=="Stop right there.");
    assert(w.command("move","",{15.5f,13.5f}).ok);
    for(int i=0;i<250;i++) w.update(.016f);
    assert(w.guard.pos.z>9.5f);
    assert(w.observationJson().find("prisonerVisible")!=std::string::npos);
    assert(w.observationJson().find("security_door")!=std::string::npos);
    // The AI receives navigable facility coordinates but not unseen hidden items.
    const auto snapshot=w.observationJson();
    assert(snapshot.find("\"facilityObjects\"")!=std::string::npos);
    assert(snapshot.find("\"security_terminal\"")!=std::string::npos);
    assert(snapshot.find("\"x\":21.5")!=std::string::npos);
    assert(snapshot.find("\"hairpin\"")==std::string::npos);
    World p;
    p.player.pos={4.5f,4.5f};
    assert(p.playerInteract().ok&&p.hasHairpin);
    p.player.pos={9.3f,6.5f};
    p.player.angle=-1.5707963f;
    assert(p.interactionHint().empty()); // Behind the player must not interact
    p.player.angle=1.5707963f;
    assert(p.interactionHint().find("CELL DOOR")!=std::string::npos);
    assert(p.playerInteract().ok&&!p.solid(10,6));
    World h;
    h.guard.pos={6.5f,5.5f};
    h.guard.destination={6.5f,14.5f};
    h.guard.angle=0;
    h.guard.moving=true;
    h.player.pos={7.8f,5.5f};
    h.noise=7;
    h.update(1.f/60.f);
    assert(distance(h.guard.destination,h.player.pos)<.1f); // Hear and investigate while patrolling.
    std::cout<<"World and agent tests passed.\n";
}
