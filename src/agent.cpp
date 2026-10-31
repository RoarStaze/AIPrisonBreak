#include "agent.hpp"
#include <algorithm>
#include <cmath>
namespace prison {
void AgentExecutor::clear(){pending.clear();issued=false;duration=0;}
bool AgentExecutor::active() const{return !pending.empty();}
size_t AgentExecutor::remaining() const{return pending.size();}
void AgentExecutor::setPlan(const std::vector<Decision>& actions){
    clear();
    for(const auto& a:actions) {
        if(pending.size()>=3)break;
        if(a.action!="speak")pending.push_back(a);
    }
    if(!pending.empty())lastEvent="New autonomous plan: "+pending.front().action;
}
std::string AgentExecutor::description() const {
    return pending.empty()?"NO ACTIVE PLAN":pending.front().action+" / "+pending.front().target+
        " ("+std::to_string(pending.size())+" steps)";
}
void AgentExecutor::tick(World& world,float dt){
    if(!active()||world.caught||world.escaped){
        if(world.caught||world.escaped)clear();
        return;
    }
    for(int n=0;n<3 && active();n++) {
        Decision& step=pending.front();
        const bool travel=step.action=="move"||step.action=="investigate"||step.action=="search";
        const std::string description=step.action+
            (step.target.empty()?std::string{}:" "+step.target);
        std::string reportedSuccess;
        if(!issued){
            const ActionResult result=world.command(step.action,step.target,step.at);
            if(!result.ok){
                lastEvent="Rejected "+description+": "+result.text;
                world.log("Officer action result: "+lastEvent,world.guard.pos);
                pending.pop_front();duration=0;issued=false;
                continue;
            }
            issued=true;duration=0;
            reportedSuccess=result.text;
            lastEvent="Executing "+description;
        }
        if(travel) {
            duration+=std::clamp(dt,0.f,.1f);
            const bool reached=distance(world.guard.pos,step.at)<=.72f;
            if(!reached && duration<28.f)return;
            if(!reached) lastEvent="Could not reach "+description+"; route obstructed or interrupted";
            else lastEvent="Completed "+description+"; arrived at destination";
        }else lastEvent="Completed "+description+
            (reportedSuccess.empty()?std::string{}:": "+reportedSuccess);
        // Only actual outcomes enter memory. "Executing" is never recorded as success.
        world.log("Officer action result: "+lastEvent,world.guard.pos);
        pending.pop_front();issued=false;duration=0;
    }
}
}
