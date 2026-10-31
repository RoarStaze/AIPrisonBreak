#include "world.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <array>
#include <queue>
#include <string>
using namespace prison;

static bool reachable(const World& w,int sx,int sz,int tx,int tz) {
    std::array<bool,World::width*World::height> visited{};
    std::queue<std::pair<int,int>> open;
    if(w.solid(sx,sz)||w.solid(tx,tz))return false;
    open.push({sx,sz});visited[sz*World::width+sx]=true;
    while(!open.empty()){
        auto [x,z]=open.front();open.pop();
        if(x==tx&&z==tz)return true;
        for(auto [dx,dz]:std::array<std::pair<int,int>,4>{{
            {1,0},{-1,0},{0,1},{0,-1}
        }}){
            int nx=x+dx,nz=z+dz;
            if(nx<0||nz<0||nx>=World::width||nz>=World::height||
               w.solid(nx,nz)||visited[nz*World::width+nx])continue;
            visited[nz*World::width+nx]=true;open.push({nx,nz});
        }
    }
    return false;
}
int main(){
    World w;
    assert(w.solid(0,0) && !w.solid(5,5));
    assert(w.solid(10,6));
    // Both walls must join the perimeter. Closing doors must seal rooms completely.
    assert(w.solid(10,World::height-2));
    assert(w.solid(18,World::height-2));
    assert(!reachable(w,5,5,14,5));
    assert(!reachable(w,14,5,22,5));
    // Tight corner collision also rejects diagonal slipping past the end.
    assert(!w.canStand({10.45f,18.5f}));
    assert(!w.canStand({18.45f,18.5f}));
    assert(!w.lineOfSight({5.5f,6.5f},{14.5f,6.5f}));
    assert(!w.command("dance").ok);
    assert(!w.command("interact","security_terminal").ok);
    assert(!w.command("move","",{0,0}).ok);
    assert(!w.command("alarm","99").ok);
    assert(w.command("unlock","cell_door").ok);
    assert(w.command("open","cell_door").ok);
    assert(!w.solid(10,6));
    assert(reachable(w,5,5,14,5));
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
    // Closed doors retain physical collision but their glass vision panels are transparent.
    World glass;
    glass.guard.pos={11.5f,6.5f};
    glass.player.pos={9.5f,6.5f};
    glass.guard.angle=-1.5707963f;
    assert(!glass.lineOfSight(glass.guard.pos,glass.player.pos));
    assert(glass.visualLineOfSight(glass.guard.pos,glass.player.pos));
    assert(glass.perceive().playerVisible);
    assert(glass.conversationObservationJson().find("\"conversationChannel\":\"door_window\"")!=std::string::npos);
    assert(glass.conversationObservationJson().find("\"prisonerVisible\":true")!=std::string::npos);
    glass.guard.angle=1.57f; // guard starts facing away but hears the prisoner at the window
    glass.engagePlayer();
    assert(glass.perceive().playerVisible);
    // Not every part of a reinforced door is glass.
    glass.guard.pos={11.5f,6.05f};
    glass.player.pos={9.5f,6.05f};
    assert(!glass.visualLineOfSight(glass.guard.pos,glass.player.pos));
    assert(!glass.perceive().playerVisible);
    // Facing a solid partition is not the same as looking through its viewport.
    glass.player.pos={9.5f,9.5f};
    glass.guard.pos={11.5f,9.5f};
    assert(!glass.visualLineOfSight(glass.guard.pos,glass.player.pos));
    auto remote=glass.conversationObservationJson();
    assert(remote.find("\"conversationChannel\":\"facility_radio\"")!=std::string::npos);
    // In-person conversation on the same side is not called a radio transmission.
    glass.player.pos={12.5f,8.5f};
    glass.guard.pos={13.5f,8.5f};
    glass.engagePlayer();
    assert(glass.conversationObservationJson().find("\"conversationChannel\":\"face_to_face\"")!=std::string::npos);
    std::cout<<"World and agent tests passed.\n";
}
