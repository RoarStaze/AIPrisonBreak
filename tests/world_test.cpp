#include "world.hpp"
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
    World p;
    p.player.pos={4.5f,4.5f};
    assert(p.playerInteract().ok&&p.hasHairpin);
    p.player.pos={9.3f,6.5f};
    assert(p.playerInteract().ok&&!p.solid(10,6));
    std::cout<<"World and agent tests passed.\n";
}
