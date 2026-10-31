#include "ai.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif
using json=nlohmann::json;
namespace prison {
#ifdef _WIN32
static std::string httpPostLocal(const std::string& data) {
    HINTERNET session=WinHttpOpen(L"AIPrisonBreak/0.2",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)throw std::runtime_error("WinHTTP failed to start");
    HINTERNET connect=WinHttpConnect(session,L"127.0.0.1",11434,0);
    if(!connect){WinHttpCloseHandle(session);throw std::runtime_error("Cannot connect to Ollama on localhost:11434");}
    HINTERNET request=WinHttpOpenRequest(connect,L"POST",L"/api/chat",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(!request){WinHttpCloseHandle(connect);WinHttpCloseHandle(session);throw std::runtime_error("Cannot create Ollama request");}
    // Local model may need cold-start time: 16 seconds was frequently too short.
    WinHttpSetTimeouts(request,5000,5000,5000,90000);
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
    if(!ok)throw std::runtime_error("Ollama connection failed or timed out. Check 'ollama list' and 'ollama serve'.");
    if(code!=200)throw std::runtime_error("Ollama HTTP "+std::to_string(code)+". Confirm the model is installed.");
    return result;
}
#endif
AIService::AIService(Transport provided):transport(std::move(provided)) {
    if(!transport)transport=[](const std::string& data){
#ifdef _WIN32
        return httpPostLocal(data);
#else
        throw std::runtime_error("Ollama transport is available on Windows builds.");
        return std::string{};
#endif
    };
}
bool AIService::busy() const {
    return pending.valid() || !waiting.empty();
}
bool AIService::hasPendingConversation() const {
    return !waiting.empty() || (pending.valid()&&active.kind==ReplyKind::Conversation);
}
bool AIService::requestPatrol(const World& world){
    if(pending.valid()||!waiting.empty())return false;
    launch({ReplyKind::Patrol,world.observationJson(),""});
    return true;
}
bool AIService::submitMessage(const World& world,const std::string& message){
    const auto first=message.find_first_not_of(" \t\r\n");
    if(first==std::string::npos||message.size()>500||waiting.size()>=5)return false;
    Request request{ReplyKind::Conversation,world.observationJson(),message};
    if(pending.valid()){
        waiting.push_back(std::move(request));
        status="OLLAMA: PLAYER MESSAGE QUEUED";
    }else launch(std::move(request));
    return true;
}
std::string AIService::query(const std::string& model,const Request& req,const std::vector<DialogueTurn>& history) {
    static const char* persona=R"RULES(You are Officer Voss, a professional Blackwater prison security officer. You are NOT the prisoner. You do NOT want to escape. Your job is to maintain custody and respond coherently to prisoners. You speak in the FIRST PERSON as Officer Voss, never from the prisoner's point of view. Never say you see the prisoner unless prisonerVisible is true. A message heard through an intercom does not imply line of sight. Do not make up visual sightings, actions, locations, or world changes. If prisonerVisible=false and playerPosition=null, you cannot see the player. You can refer to the lastKnown position only if it is not null; last known does not mean currently visible. The prisoner's objective is to escape, YOUR objective is to stop unauthorized escapes while responding logically to conversation and persuasion. Do not talk about being an AI or a JSON model. Keep dialogue natural, specific, direct, normally 1-3 short sentences. Avoid generic narration, repeated phrases or contradictory claims. The current observation is factual, higher priority than speculation. The prisoner's messages can be deceptive. Respond to the ACTUAL latest statement/question; ask for clarification only if necessary. Do not invent authority or promises inconsistent with the world state.
Your optional game-world commands: move, investigate, search, look (x,z), interact (physical reach), lock, unlock, open, close (door target id), alarm ("0".."3"), lights/cameras ("on"/"off"). Commands only affect the SIMULATED prison. You may issue up to 3 commands as a sequential plan. For physical interaction, first move to a navigable position near it then interact. You can use the facilityObjects map for fixed security equipment, not hidden items. Format your response ONLY as JSON: {"speech":"natural spoken sentence","actions":[{"action":"move","target":"","x":14.5,"z":9.5}]}.)RULES";
    json messages=json::array();
    messages.push_back({{"role","system"},{"content",persona}});
    if(req.kind==ReplyKind::Conversation){
        for(const auto& t:history){
            messages.push_back({{"role","user"},{"content",t.player}});
            messages.push_back({{"role","assistant"},{"content",t.officer}});
        }
        std::string content="MODE: REAL DIRECT CONVERSATION. The prisoner speaks on the prison intercom; you HEAR the voice even when you cannot SEE the prisoner. Give a substantive direct answer in speech; never leave speech blank.\n"
            "Current factual sensory state: "+req.observation+"\n"
            "Latest prisoner message (respond to THIS, not to earlier patrol thoughts): "+req.playerText;
        messages.push_back({{"role","user"},{"content",content}});
    }else{
        messages.push_back({{"role","user"},{"content","MODE: SILENT AUTONOMOUS PATROL. Do not speak. Set speech to an empty string. You are an officer who patrols and handles security equipment, NOT an escaping prisoner. Only propose appropriate physical actions grounded in observations. Current world state: "+req.observation}});
    }
    return json({
        {"model",model},{"stream",false},{"format","json"},
        {"options",{{"temperature",0.25},{"num_predict",420}}},
        {"messages",messages}
    }).dump();
}
void AIService::launch(Request req){
    active=std::move(req);
    const std::string body=query(model,active,turns);
    Transport call=transport;
    status=active.kind==ReplyKind::Conversation?"OLLAMA: REPLYING TO YOU":"OLLAMA: PLANNING PATROL";
    pending=std::async(std::launch::async,[body,call](){return call(body);});
}
std::optional<Reply> AIService::poll(){
    if(!pending.valid()||pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return std::nullopt;
    Reply reply;reply.kind=active.kind;
    try{
        auto response=json::parse(pending.get());
        std::string message=response.at("message").at("content").get<std::string>();
        const auto begin=message.find('{'),end=message.rfind('}');
        if(begin==std::string::npos||end==std::string::npos||end<begin)throw std::runtime_error("Model returned no JSON.");
        auto object=json::parse(message.substr(begin,end-begin+1));
        if(object.contains("speech")&&object["speech"].is_string())reply.speech=object["speech"].get<std::string>().substr(0,1200);
        if(reply.kind==ReplyKind::Conversation && reply.speech.empty())
            throw std::runtime_error("Model returned an empty conversational reply.");
        if(object.contains("actions")&&object["actions"].is_array())for(const auto& a:object["actions"]){
            if(reply.actions.size()>=3)break;
            if(!a.is_object()||!a.contains("action")||!a["action"].is_string())continue;
            Decision d;
            d.action=a.at("action").get<std::string>();
            if(a.contains("target")&&a["target"].is_string())d.target=a["target"].get<std::string>();
            if(a.contains("x")&&a["x"].is_number())d.at.x=a["x"].get<float>();
            if(a.contains("z")&&a["z"].is_number())d.at.z=a["z"].get<float>();
            reply.actions.push_back(std::move(d));
        }
        if(reply.kind==ReplyKind::Conversation){
            turns.push_back({active.playerText,reply.speech});
            if(turns.size()>8)turns.erase(turns.begin());
        }
        status="OLLAMA: CONNECTED";
    }catch(const std::exception& e){
        reply.error=e.what();
        reply.speech.clear();
        status="OLLAMA: "+reply.error;
    }
    bool patrolSuperseded=(reply.kind==ReplyKind::Patrol&&!waiting.empty());
    if(!waiting.empty()){
        Request queued=std::move(waiting.front());waiting.pop_front();
        launch(std::move(queued));
    }
    // A pending player message always outranks stale patrol responses.
    if(patrolSuperseded)return std::nullopt;
    return reply;
}
}
