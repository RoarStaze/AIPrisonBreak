#include "ai.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif
using json=nlohmann::json;
namespace prison {
bool AIService::busy() const {return pending.valid() && pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready;}
#ifdef _WIN32
static std::string httpPostLocal(const std::string& data) {
    HINTERNET session=WinHttpOpen(L"AIPrisonBreak/0.1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)throw std::runtime_error("WinHTTP initialization failed");
    HINTERNET connect=WinHttpConnect(session,L"127.0.0.1",11434,0);
    if(!connect){WinHttpCloseHandle(session);throw std::runtime_error("Could not connect to local Ollama");}
    HINTERNET request=WinHttpOpenRequest(connect,L"POST",L"/api/chat",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(!request){WinHttpCloseHandle(connect);WinHttpCloseHandle(session);throw std::runtime_error("Could not make request");}
    WinHttpSetTimeouts(request,2500,2500,2500,16000);
    bool ok=WinHttpSendRequest(request,L"Content-Type: application/json\r\n",-1L,(LPVOID)data.data(),(DWORD)data.size(),(DWORD)data.size(),0)
        && WinHttpReceiveResponse(request,nullptr);
    std::string result;
    if(ok)for(;;){
        DWORD count=0;if(!WinHttpQueryDataAvailable(request,&count)||!count)break;
        if(result.size()+count>150000)break;
        std::string buf(count,'\0');DWORD read=0;
        if(!WinHttpReadData(request,buf.data(),count,&read))break;
        result.append(buf.data(),read);
    }
    DWORD code=0,length=sizeof(code);
    WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&length,WINHTTP_NO_HEADER_INDEX);
    WinHttpCloseHandle(request);WinHttpCloseHandle(connect);WinHttpCloseHandle(session);
    if(!ok||code!=200)throw std::runtime_error("Ollama offline, model missing, or response error (HTTP "+std::to_string(code)+")");
    return result;
}
#endif
std::string AIService::query(const std::string& model,const std::string& observation,const std::string& playerMessage) {
    // The model controls in-game actions only. World validates every command.
    static const char* rules=R"(You are Officer Voss, an autonomous prison security officer inside BLACKWATER, a 3D simulation. You have goals: prevent escape, maintain order, respond to threats, negotiate with the prisoner. You are NOT writing a story: choose physical actions with direct world consequences. Use ONLY information in the observation. Your body can move, look, investigate, search and physically interact with nearby objects. Your security tablet can lock/unlock/open/close doors (cell_door, service_door, security_door, maintenance_door, exit_gate), set alarm 0-3, switch lights on/off, switch cameras on/off. You have authority to control game-world systems, NOT real computers. Choose up to 3 actions from: move, investigate, search, look (use x and z numbers), interact (target id, requires proximity), lock, unlock, open, close (target door id), alarm (target "0"-"3"), lights, cameras (target "on"/"off"), speak (target text). The facilityObjects list is your prison security floorplan with coordinates; it includes fixed equipment even when out of sight but only includes portable items you can observe. Doors also have mapped x/z positions. For a physical interaction plan, first move near the object, THEN use interact as the next step; these actions execute sequentially over time. Remote door, CCTV, alarm and lighting commands do not require proximity. The prisoner may bluff; be skeptical. Answer strict JSON: {"actions":[{"action":"...","target":"...","x":0,"z":0}],"speech":"one short line said aloud"}. Do NOT output markdown, explanations or anything outside JSON. If player is visible and escaping, investigate/pursue; if talking, respond tactically. Decisions must be actionable.)";
    json req={
        {"model",model},{"stream",false},{"format","json"},
        {"options",{{"temperature",0.35},{"num_predict",220}}},
        {"messages",json::array({
            {{"role","system"},{"content",rules}},
            {{"role","user"},{"content",std::string("Current sensory observation: ")+observation+"\nPrisoner said: "+playerMessage}}
        })}
    };
    return req.dump();
}
bool AIService::request(const World& world,const std::string& playerMessage){
    if(busy())return false;
    if(pending.valid()) {try{pending.get();}catch(...){}}
    const std::string payload=query(model,world.observationJson(),playerMessage);
    status="OLLAMA: THINKING";
    pending=std::async(std::launch::async,[payload](){
#ifdef _WIN32
        return httpPostLocal(payload);
#else
        throw std::runtime_error("Ollama WinHTTP transport is implemented for Windows builds");
        return std::string{};
#endif
    });
    return true;
}
std::optional<Reply> AIService::poll() {
    if(!pending.valid()||pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return std::nullopt;
    try{
        auto response=json::parse(pending.get());
        std::string message=response.at("message").at("content").get<std::string>();
        size_t start=message.find('{'),end=message.rfind('}');
        if(start==std::string::npos||end==std::string::npos||end<start)throw std::runtime_error("Model returned no JSON");
        auto obj=json::parse(message.substr(start,end-start+1));
        Reply reply;
        if(obj.contains("speech")&&obj["speech"].is_string())reply.speech=obj["speech"].get<std::string>().substr(0,240);
        if(obj.contains("actions")&&obj["actions"].is_array())for(const auto& x:obj["actions"]){
            if(reply.actions.size()==3)break;
            if(!x.is_object()||!x.value("action",json()).is_string())continue;
            Decision d;d.action=x.value("action",std::string{});d.target=x.value("target",std::string{});
            d.at={x.value("x",0.f),x.value("z",0.f)};reply.actions.push_back(d);
        }
        status="OLLAMA: CONNECTED";
        return reply;
    }catch(const std::exception& e){status=std::string("OLLAMA: ")+e.what();return std::nullopt;}
}
}
