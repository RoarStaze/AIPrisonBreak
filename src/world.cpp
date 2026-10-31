#include "world.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <queue>
#include <sstream>

namespace prison {
float distance(Point a,Point b) {return std::hypot(a.x-b.x,a.z-b.z);}
static std::string quoted(const std::string& value) {
    std::string out="\"";
    for(char c:value) { if(c=='"'||c=='\\') out+='\\'; if(c=='\n') out+="\\n"; else if(static_cast<unsigned char>(c)<32) out+=' '; else out+=c; }
    return out+"\"";
}
World::World() {
    doors={
        {"cell_door",10,6,false,true,false},
        {"service_door",10,13,false,true,false},
        {"security_door",18,5,false,true,true},
        {"maintenance_door",18,12,false,true,true},
        {"exit_gate",26,16,false,true,true}
    };
    objects={
        {"hairpin","item",{4.5f,4.5f}},
        {"keycard","item",{21.5f,6.5f}},
        {"security_terminal","terminal",{21.5f,4.5f}},
        {"generator","generator",{22.5f,13.5f}},
        {"emergency_exit","exit",{26.5f,16.5f}}
    };
    guard.destination={14.5f,14.5f};
    log("Blackwater: Block C entered emergency lockdown.",{14,9});
}
const Door* World::doorAt(int x,int z) const {
    for(const auto& d:doors) if(d.x==x&&d.z==z) return &d;
    return nullptr;
}
bool World::solid(int x,int z) const {
    if(x<1||z<1||x>=width-1||z>=height-1) return true;
    // Walls join the perimeter; no gap at the last interior row.
    if(x==10 && z>=1 && z<height-1) {
        auto* d=doorAt(x,z); return !d || !d->open;
    }
    if(x==18 && z>=1 && z<height-1) {
        auto* d=doorAt(x,z); return !d || !d->open;
    }
    if(z==10 && x>=19 && x<=26) return true;
    if(x==26&&z==16) {auto* d=doorAt(x,z); return !d||!d->open;}
    return false;
}
bool World::canStand(Point p,float radius) const {
    const Point corners[]={{p.x-radius,p.z-radius},{p.x+radius,p.z-radius},{p.x-radius,p.z+radius},{p.x+radius,p.z+radius}};
    for(Point q:corners) if(solid((int)std::floor(q.x),(int)std::floor(q.z))) return false;
    return true;
}
bool World::lineOfSight(Point a,Point b) const {
    float dist=distance(a,b);
    int n=std::max(1,(int)std::ceil(dist/.12f));
    for(int i=1;i<n;i++) {float t=(float)i/n; Point p={a.x+(b.x-a.x)*t,a.z+(b.z-a.z)*t};if(solid((int)std::floor(p.x),(int)std::floor(p.z))) return false;}
    return true;
}

bool World::visualLineOfSight(Point from,Point to) const {
    const float len=distance(from,to);
    const int n=std::max(1,(int)std::ceil(len/.07f));
    for(int i=1;i<n;i++){
        const float t=(float)i/n;
        Point p={from.x+(to.x-from.x)*t,from.z+(to.z-from.z)*t};
        int x=(int)std::floor(p.x),z=(int)std::floor(p.z);
        if(!solid(x,z))continue;
        const Door* door=doorAt(x,z);
        if(door && !door->open && std::abs(p.z-(z+.5f))<.215f)continue;
        return false;
    }
    return true;
}
void World::engagePlayer(){
    if(distance(player.pos,guard.pos)>4.5f || !visualLineOfSight(guard.pos,player.pos))return;
    guard.angle=std::atan2(player.pos.x-guard.pos.x,-(player.pos.z-guard.pos.z));
    guard.moving=false;
}
void World::log(std::string message,Point at,float loudness) {
    events.push_back({elapsed,std::move(message),at,loudness});
    while(events.size()>40) events.pop_front();
}
void World::movePlayer(Point delta,bool crouch,bool run) {
    if(caught||escaped) return;
    float mag=std::hypot(delta.x,delta.z); if(mag>1) {delta.x/=mag;delta.z/=mag;mag=1;}
    player.crouching=crouch;
    bool sprint=run && !crouch && player.stamina>.02f;
    float speed=crouch?1.45f:(sprint?4.3f:2.5f);
    Point target={player.pos.x+delta.x*speed/60.f,player.pos.z+delta.z*speed/60.f};
    if(canStand({target.x,player.pos.z}))player.pos.x=target.x;
    if(canStand({player.pos.x,target.z}))player.pos.z=target.z;
    player.moving=mag>.03f;
    if(sprint&&mag>.03f) player.stamina=std::max(0.f,player.stamina-.003f);
    else player.stamina=std::min(1.f,player.stamina+.0016f);
    noise=player.moving?(crouch?.35f:(sprint?7.f:2.f)):0;
    if(sprint&&player.moving&&(int)(elapsed*2)!=(int)((elapsed-1.f/60)*2))log("Running footsteps",player.pos,7);
}
std::optional<Point> World::nextPathPoint(Point from,Point to) const {
    auto ix=[](Point p){return std::clamp((int)std::floor(p.x),0,width-1)+width*std::clamp((int)std::floor(p.z),0,height-1);};
    int start=ix(from), end=ix(to); if(start==end)return to;
    std::array<int,width*height> parent;parent.fill(-1);parent[start]=start;
    std::queue<int> q;q.push(start);
    while(!q.empty()) {
        int c=q.front();q.pop();if(c==end)break;
        int x=c%width,z=c/width;
        const int dx[]={1,-1,0,0},dz[]={0,0,1,-1};
        for(int i=0;i<4;i++){int nx=x+dx[i],nz=z+dz[i];if(nx<0||nz<0||nx>=width||nz>=height||solid(nx,nz))continue;
            int k=nz*width+nx;if(parent[k]!=-1)continue;parent[k]=c;q.push(k);}
    }
    if(parent[end]==-1)return std::nullopt;
    int node=end;
    while(parent[node]!=start && node!=start)node=parent[node];
    return Point{node%width+.5f,node/width+.5f};
}
bool World::tryMove(Actor& a,Point destination,float step) {
    float d=distance(a.pos,destination);if(d<.12f)return true;
    float dx=(destination.x-a.pos.x)/d,dz=(destination.z-a.pos.z)/d;
    a.angle=std::atan2(dx,-dz);
    Point p={a.pos.x+dx*std::min(step,d),a.pos.z+dz*std::min(step,d)};
    if(canStand({p.x,a.pos.z}))a.pos.x=p.x;
    if(canStand({a.pos.x,p.z}))a.pos.z=p.z;
    return distance(a.pos,destination)<.2f;
}
void World::followGuard(float dt) {
    if(caught||escaped)return;
    Observation obs=perceive();
    if(obs.playerVisible) {
        guard.destination=player.pos;lastKnown=player.pos;hasLastKnown=true;lastSeen=elapsed;
        if(distance(guard.pos,player.pos)<.85f){caught=true;status="OFFICER VOSS DETAINED YOU";log("Player detained.",guard.pos);return;}
    } else if(obs.playerAudible){guard.destination=player.pos;lastKnown=player.pos;hasLastKnown=true;alert=std::max(alert,1);}
    Point target=guard.destination;
    if(auto next=nextPathPoint(guard.pos,target))tryMove(guard,*next,dt*(obs.playerVisible?3.25f:1.65f));
    if(distance(guard.pos,target)<.7f && !obs.playerVisible){
        guard.moving=false;
        if(target.z>12)guard.destination={14.5f,4.5f};
        else guard.destination={14.5f,14.5f};
    }else guard.moving=true;
}
void World::update(float dt) {
    dt=std::clamp(dt,0.f,.05f);
    elapsed+=dt; followGuard(dt);
    if(cameras && !caught) {
        // Cameras report movement in common areas, not through walls.
        for(Point c: {Point{13.5f,3.5f},Point{22.5f,15.5f}}){
            if(distance(c,player.pos)<5 && lineOfSight(c,player.pos) && player.moving && !player.crouching){
                lastKnown=player.pos;hasLastKnown=true;alert=std::max(alert,2);
                if((int)elapsed%10==0)status="CCTV MOTION DETECTED";
            }
        }
    }
}
Observation World::perceive() const {
    Observation o;Point p=player.pos;float dist=distance(guard.pos,p);
    float dx=std::sin(guard.angle),dz=-std::cos(guard.angle);
    float dot=(dx*(p.x-guard.pos.x)+dz*(p.z-guard.pos.z))/std::max(.001f,dist);
    o.playerVisible=dist<.85f||(dist<(lights?9.f:4.5f) && dot>.24f && visualLineOfSight(guard.pos,p));
    o.playerAudible=noise>0 && dist<noise+1 && lineOfSight(guard.pos,p);
    o.playerLastKnown=lastKnown;
    for(const auto& d:doors)if(distance(guard.pos,{d.x+.5f,d.z+.5f})<10 && lineOfSight(guard.pos,{d.x+.5f,d.z+.5f}))
        o.visibleObjects.push_back(d.id+(d.open?" OPEN":d.locked?" LOCKED":" UNLOCKED"));
    for(const auto& obj:objects)if(!obj.taken&&distance(guard.pos,obj.pos)<8&&lineOfSight(guard.pos,obj.pos))
        o.visibleObjects.push_back(obj.id);
    for(auto it=events.rbegin();it!=events.rend()&&o.reports.size()<8;++it) {
        if(elapsed-it->time>75)break;
        o.reports.push_back(it->message);
        if(it->loudness>0&&elapsed-it->time<8&&distance(it->source,guard.pos)<it->loudness+1) o.sounds.push_back(it->message);
    }
    if(cameras)for(Point c:{Point{13.5f,3.5f},Point{22.5f,15.5f}})
        if(distance(c,p)<6&&lineOfSight(c,p)&&player.moving)o.reports.push_back("CCTV last known prisoner location "+std::to_string((int)p.x)+","+std::to_string((int)p.z));
    return o;
}
ActionResult World::command(const std::string& action,const std::string& target,Point at) {
    if(caught||escaped)return {false,"Game ended."};
    if(action=="speak") {
        if(target.empty()) return {false,"No speech."};
        guardSpeech=target.substr(0,1200); log("Voss: "+guardSpeech,guard.pos);return {true,guardSpeech};
    }
    if(action=="move"||action=="investigate"||action=="search") {
        if(at.x<1||at.z<1||at.x>width-2||at.z>height-2||solid((int)at.x,(int)at.z))return {false,"Destination blocked."};
        guard.destination=at;guard.moving=true;return {true,"Guard moving."};
    }
    if(action=="look") {guard.angle=std::atan2(at.x-guard.pos.x,-(at.z-guard.pos.z));return {true,"Guard changed gaze."};}
    if(action=="alarm") {if(target!="0"&&target!="1"&&target!="2"&&target!="3")return {false,"Alarm must be 0..3."};
        alert=target[0]-'0';lockdown=alert>=2;log("Security alarm set to level "+target,guard.pos,5);return {true,"Alarm changed."};}
    if(action=="lights") {lights=target=="on";log(lights?"Lights restored.":"Lights cut.",guard.pos);return {true,"Lighting changed."};}
    if(action=="cameras"){cameras=target=="on";log(cameras?"CCTV enabled.":"CCTV disabled.",guard.pos);return {true,"CCTV changed."};}
    if(action=="lock"||action=="unlock"||action=="open"||action=="close") {
        for(auto& d:doors)if(d.id==target) {
            if(action=="lock") {d.locked=true;d.open=false;}
            if(action=="unlock")d.locked=false;
            if(action=="open"){if(d.locked)return{false,"Door locked."};d.open=true;}
            if(action=="close")d.open=false;
            log("Guard "+action+" "+target,guard.pos,2);
            return {true,"Door "+action+" successful."};
        }
        return {false,"Unknown door."};
    }
    if(action=="interact") {
        for(auto& obj:objects)if(obj.id==target) {
            if(distance(guard.pos,obj.pos)>1.8f||!lineOfSight(guard.pos,obj.pos))return{false,"Object not within reach."};
            if(obj.id=="generator") {lights=!lights;log("Generator toggled.",obj.pos,5);return{true,"Generator toggled."};}
            if(obj.id=="security_terminal") {cameras=!cameras;return{true,"CCTV toggled."};}
            if(obj.kind=="item"){obj.taken=true;log("Guard collected "+target,obj.pos);return{true,"Item secured."};}
            return{false,"No guard action."};
        }
        return {false,"Unknown object."};
    }
    return{false,"Unrecognized or unauthorized action."};
}
// Targeting is the same for HUD prompts and actual interaction: the
// crosshair must point toward an item or doorway within physical reach.
static bool inFront(Point origin,float yaw,Point target) {
    float dx=target.x-origin.x, dz=target.z-origin.z;
    float len=std::hypot(dx,dz);
    return len<.35f || ((dx*std::sin(yaw)-dz*std::cos(yaw))/len)>.36f;
}
static std::string readableName(const std::string& id) {
    std::string s=id;
    for(char& c:s)if(c=='_')c=' ';
    for(char& c:s)c=(char)std::toupper(static_cast<unsigned char>(c));
    return s;
}
std::string World::interactionHint() const {
    if(caught||escaped)return "";
    float best=2.15f;
    std::string result;
    for(const auto& obj:objects){
        if(obj.taken)continue;
        float dist=distance(player.pos,obj.pos);
        if(dist>2.05f||dist>=best||!inFront(player.pos,player.angle,obj.pos))continue;
        if(!lineOfSight(player.pos,obj.pos))continue;
        best=dist;
        if(obj.kind=="item")result="E  TAKE "+readableName(obj.id);
        else if(obj.id=="emergency_exit")result="E  ESCAPE";
        else result="E  USE "+readableName(obj.id);
    }
    for(const auto& door:doors) {
        Point p={door.x+.5f,door.z+.5f};
        float dist=distance(player.pos,p);
        if(dist>2.05f||dist>=best||!inFront(player.pos,player.angle,p))continue;
        best=dist;
        result=door.locked?"E  UNLOCK "+readableName(door.id):
            (door.open?"E  CLOSE ":"E  OPEN ")+readableName(door.id);
    }
    return result;
}
ActionResult World::playerInteract() {
    if(caught||escaped)return{false,"Game ended."};
    std::string prompt=interactionHint();
    if(prompt.empty())return{false,"Look directly at an item or door to interact."};
    // Select by the exact prompt that the player sees.
    for(auto& obj:objects){
        if(obj.taken||prompt.find(readableName(obj.id))==std::string::npos)continue;
        if(obj.id=="hairpin"){hasHairpin=true;obj.taken=true;status="HAIRPIN ACQUIRED";return{true,"You found a hairpin."};}
        if(obj.id=="keycard"){hasCard=true;obj.taken=true;status="KEYCARD ACQUIRED";return{true,"Security keycard acquired."};}
        if(obj.id=="security_terminal"){cameras=false;alert=std::max(alert,2);log("Security terminal breached.",obj.pos,8);return{true,"CCTV disabled."};}
        if(obj.id=="generator"){lights=false;log("Power shutdown.",obj.pos,8);return{true,"Power cut."};}
        if(obj.id=="emergency_exit"){
            if(doorAt(26,16)->open){escaped=true;status="YOU ESCAPED BLACKWATER";return{true,"Freedom."};}
            return {false,"Unlock the exit gate first."};
        }
    }
    for(auto& door:doors){
        if(prompt.find(readableName(door.id))==std::string::npos)continue;
        if(door.locked){
            if(door.card&&!hasCard)return{false,"Security keycard required."};
            if(!door.card&&!hasHairpin)return{false,"Find something to pick this lock."};
            door.locked=false;log("Lock tampered: "+door.id,player.pos,4);
        }
        door.open=!door.open;
        status=door.open?"DOOR OPENED":"DOOR CLOSED";
        return{true,status};
    }
    return{false,"Nothing within reach."};
}
std::string World::observationJson() const {
    Observation o=perceive();
    std::ostringstream s;s<<"{\"time\":"<<(int)elapsed<<",\"guard\":{\"x\":"<<guard.pos.x<<",\"z\":"<<guard.pos.z<<",\"facing\":"<<guard.angle
      <<"},\"prisonerVisible\":"<<(o.playerVisible?"true":"false")<<",\"prisonerAudible\":"<<(o.playerAudible?"true":"false")
      <<",\"lastKnown\":"<<(hasLastKnown?(std::string("{\"x\":")+std::to_string(lastKnown.x)+",\"z\":"+std::to_string(lastKnown.z)+"}"):std::string("null"))
      <<",\"prisonerPosition\":"<<(o.playerVisible?(std::string("{\"x\":")+std::to_string(player.pos.x)+",\"z\":"+std::to_string(player.pos.z)+"}"):std::string("null"))
      <<",\"prisonerZoneIfVisible\":"<<(o.playerVisible?quoted(player.pos.x<10.f?"cell_wing":player.pos.x<18.f?"main_corridor":"security_wing"):std::string("null"))
      <<",\"guardMoving\":"<<(guard.moving?"true":"false")<<",\"alarm\":"<<alert
      <<",\"lockdown\":"<<(lockdown?"true":"false")<<",\"cameras\":"<<(cameras?"true":"false")<<",\"lights\":"<<(lights?"true":"false")<<",\"doors\":[";
    for(size_t i=0;i<doors.size();i++){const auto& d=doors[i];if(i)s<<",";s<<"{\"id\":"<<quoted(d.id)<<",\"x\":"<<(d.x+.5f)<<",\"z\":"<<(d.z+.5f)<<",\"locked\":"<<(d.locked?"true":"false")<<",\"open\":"<<(d.open?"true":"false")<<"}";}
    // Facility blueprints disclose fixed equipment; portable items require observation.
    // This distinction prevents the language model from learning hidden loot coordinates.
    s<<"],\"facilityObjects\":[";
    bool firstObject=true;
    for(const auto& obj:objects){
        bool visible=distance(guard.pos,obj.pos)<8.f&&lineOfSight(guard.pos,obj.pos);
        if(obj.kind=="item"&&!visible)continue;
        if(!firstObject)s<<",";
        firstObject=false;
        s<<"{\"id\":"<<quoted(obj.id)<<",\"kind\":"<<quoted(obj.kind)
         <<",\"x\":"<<obj.pos.x<<",\"z\":"<<obj.pos.z
         <<",\"taken\":"<<(obj.taken?"true":"false")<<"}";
    }
    s<<"],\"visibleObjects\":[";for(size_t i=0;i<o.visibleObjects.size();i++){if(i)s<<",";s<<quoted(o.visibleObjects[i]);}
    s<<"],\"reports\":[";for(size_t i=0;i<o.reports.size();i++){if(i)s<<",";s<<quoted(o.reports[i]);}
    s<<"],\"sounds\":[";for(size_t i=0;i<o.sounds.size();i++){if(i)s<<",";s<<quoted(o.sounds[i]);}
    s<<"]}";return s.str();
}

std::string World::conversationObservationJson() const {
    std::string state=observationJson();
    if(state.empty() || state.back()!='}')return state;
    state.pop_back();
    const float range=distance(player.pos,guard.pos);
    const bool sight=visualLineOfSight(guard.pos,player.pos);
    const bool physical=lineOfSight(guard.pos,player.pos);
    const bool local=range<=4.5f && sight;
    const std::string channel=local?(physical?"face_to_face":"door_window"):"facility_radio";
    std::ostringstream ss;
    ss<<state<<",\"conversationChannel\":"<<quoted(channel)
      <<",\"prisonerVisibleAtConversation\":"<<(perceive().playerVisible?"true":"false")
      <<",\"prisonerAtDoorWindow\":"<<(channel=="door_window"?"true":"false")
      <<",\"nearbySpeaker\":"<<(local?"true":"false")
      <<",\"distanceMetersIfObserved\":";
    if(local)ss<<range;
    else ss<<"null";
    ss<<",\"verifiedFacilityCapabilities\":{"
      <<"\"security_terminal\":\"CCTV toggling only\","
      <<"\"identity_database\":false,"
      <<"\"intercom_pad\":false,"
      <<"\"officerCanVerifyIdentity\":false,"
      <<"\"physicalDoorsBlockMovement\":true}}";
    return ss.str();
}
std::string World::stateJson()const{
    std::ostringstream s;s<<"{\"time\":"<<elapsed<<",\"player\":{\"x\":"<<player.pos.x<<",\"z\":"<<player.pos.z
       <<"},\"guard\":{\"x\":"<<guard.pos.x<<",\"z\":"<<guard.pos.z<<"},\"escaped\":"<<(escaped?"true":"false")
       <<",\"caught\":"<<(caught?"true":"false")<<"}";return s.str();
}
}
