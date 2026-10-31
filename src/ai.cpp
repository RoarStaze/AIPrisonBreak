#include "ai.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <set>
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
void AIService::noteOutcome(const std::string& result) {
    if(result.empty())return;
    outcomes.push_back(result.substr(0,240));
    if(outcomes.size()>12)outcomes.pop_front();
}
static std::set<std::string> dialogueWords(const std::string& spoken) {
    std::set<std::string> words;
    std::string word;
    for(unsigned char c:spoken) {
        if(std::isalnum(c)) word+=static_cast<char>(std::tolower(c));
        else if(!word.empty()) {words.insert(word);word.clear();}
    }
    if(!word.empty())words.insert(word);
    return words;
}
static bool repeatsEarlierAnswer(const std::string& answer,const std::string& earlier) {
    if(answer.size()<24||earlier.size()<24)return false;
    auto current=dialogueWords(answer),previous=dialogueWords(earlier);
    if(current.empty()||previous.empty())return false;
    int common=0;
    for(const auto& word:current)if(previous.count(word))common++;
    int biggest=static_cast<int>((std::max)(current.size(),previous.size()));
    return (float)common/(float)biggest>=.88f;
}

static std::string conflictWithObservedWorld(const std::string& speech,const std::string& observation) {
    json fact;
    try{fact=json::parse(observation);}catch(...){return {};}
    std::string lower=speech;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char x){return (char)std::tolower(x);});
    auto mentions=[&](const char* words){return lower.find(words)!=std::string::npos;};
    if(fact.value("prisonerVisible",false) &&
       (mentions("cannot see you")||mentions("can't see you")||
        mentions("don't see you")||mentions("cannot see your face")||
        mentions("can't see your face")||mentions("only hear you"))) {
        return "You actually SEE the prisoner at this location; never deny the current visible observation.";
    }
    const auto channel=fact.value("conversationChannel",std::string{});
    if((mentions("intercom pad")||mentions("standing on the intercom")) ||
       (channel!="facility_radio" && (mentions("through the intercom")||
        mentions("over the intercom")))) {
        return "No intercom pad exists; the prisoner is speaking "+channel+
               ". Refer to nearby physical presence accurately.";
    }
    if(mentions("identity trace confirmed")||mentions("i've checked every log")||
       mentions("i ran an identity trace")||mentions("i've run every access log")) {
        return "No identity trace or facility identity database exists. Do not claim to have run one.";
    }
    return {};
}
static std::string safeGroundedAnswer(const std::string& observation,const std::string& violation) {
    json fact;
    try{fact=json::parse(observation);}catch(...){return "I need to verify what is happening before I answer."; }
    if(violation.find("identity")!=std::string::npos)
        return "I cannot verify your identity with the equipment available to me. What do you need?";
    const auto channel=fact.value("conversationChannel",std::string{});
    if(fact.value("prisonerVisible",false) && channel=="door_window")
        return "I can see you through the security door's window. The locked door remains between us.";
    if(fact.value("prisonerVisible",false))
        return "Yes, I can see you here. I'm listening. What do you need?";
    return "I can hear you over the facility radio, but I cannot see your current position.";
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
    Request request{ReplyKind::Conversation,world.conversationObservationJson(),message};
    if(pending.valid()){
        waiting.push_back(std::move(request));
        status="OLLAMA: PLAYER MESSAGE QUEUED";
    }else launch(std::move(request));
    return true;
}
std::string AIService::query(const std::string& model,const Request& req,const std::vector<DialogueTurn>& history,
                             const std::deque<std::string>& outcomes) {
    static const char* persona=R"RULES(You are Officer Voss, a professional Blackwater prison security officer. You are NOT the prisoner. You do NOT want to escape. Your job is to maintain custody and respond coherently to prisoners. You speak in the FIRST PERSON as Officer Voss, never from the prisoner's point of view. Never say you see the prisoner unless prisonerVisible is true. A remote facility radio conversation does not imply line of sight; a nearby person or glazed door window may be directly observed. Do not invent sightings, actions, locations, identity scans, equipment, or world changes. The verifiedFacilityCapabilities field is authoritative. If prisonerVisible=false and playerPosition=null, you cannot see the player. You can refer to the lastKnown position only if it is not null; last known does not mean currently visible. The prisoner's objective is to escape, YOUR objective is to stop unauthorized escapes while responding logically to conversation and persuasion. Do not talk about being an AI or a JSON model. Keep dialogue natural, specific, direct, normally 1-3 short sentences. Avoid generic narration, repeated phrases or contradictory claims. Treat the previous conversation as real events, not disposable boilerplate. When asked a new question, give new actionable details based on the factual situation. If refusing something, explain concrete alternatives when appropriate; do not automatically repeat "stay in your cell." A prisoner may express fear, ask for care, deceive you or bargain; respond to their actual intent with professional judgment. Maintain coherent commitments and tone between turns. The officer can revise plans when prior actions failed and must not pretend the actions succeeded. The current observation is factual, higher priority than speculation. The prisoner's messages can be deceptive. Respond to the ACTUAL latest statement/question; ask for clarification only if necessary. Do not invent authority or promises inconsistent with the world state.
Your optional game-world commands: move, investigate, search, look (x,z), interact (physical reach), lock, unlock, open, close (door target id), alarm ("0".."3"), lights/cameras ("on"/"off"). Commands only affect the SIMULATED prison. You may issue up to 3 commands as a sequential plan. For physical interaction, first move to a navigable position near it then interact. You can use the facilityObjects map for fixed security equipment, not hidden items. Format your response ONLY as JSON: {"speech":"natural spoken sentence","actions":[{"action":"move","target":"","x":14.5,"z":9.5}]}.)RULES";
    json messages=json::array();
    messages.push_back({{"role","system"},{"content",persona}});
    std::string outcomeContext="\nYour recent VERIFIED actions and outcomes (use to improve plans; do not claim unverified success):";
    for(const auto& x:outcomes)outcomeContext+="\n- "+x;
    if(req.kind==ReplyKind::Conversation){
        const size_t begin=history.size()>14?history.size()-14:0;
        for(size_t i=begin;i<history.size();i++){
            const auto& t=history[i];
            messages.push_back({{"role","user"},{"content",t.player}});
            messages.push_back({{"role","assistant"},{"content",t.officer}});
        }
        std::string content="MODE: CONTEXTUAL DIRECT CONVERSATION. Read conversationChannel and prisonerVisible from the current observation. face_to_face means you are nearby speaking directly; door_window means you see and hear the prisoner through the door glazing while the physical door remains locked; facility_radio means you only hear a remote voice without seeing the prisoner. NEVER invent an intercom pad or claim to be standing on it. If prisonerVisible=true, you see the prisoner; do not deny it. Do not invent movements, log checks or identity scans. Use verifiedFacilityCapabilities for equipment facts. Answer the specific question naturally and accurately; speech must not be empty.\n"
            "Current factual sensory state: "+req.observation+outcomeContext+"\n"
            "Latest prisoner message (respond to THIS, not to earlier patrol thoughts): "+req.playerText+
            (req.retryHint.empty()?std::string{}:("\nREPAIR: Your last attempt was rejected for repetition. "+req.retryHint+
                 " Avoid repeating the prior refusal verbatim; be specific about this NEW question."));
        messages.push_back({{"role","user"},{"content",content}});
    }else{
        messages.push_back({{"role","user"},{"content","MODE: SILENT AUTONOMOUS PATROL. Do not speak. Set speech to an empty string. You are an officer who patrols and handles security equipment, NOT an escaping prisoner. Only propose appropriate physical actions grounded in observations. Current world state: "+req.observation+outcomeContext}});
    }
    return json({
        {"model",model},{"stream",false},{"format","json"},
        {"think",false},
        {"keep_alive","10m"},
        {"options",{{"temperature",req.kind==ReplyKind::Conversation?0.42:0.22},
                    {"num_ctx",6144},{"repeat_penalty",1.13},
                    {"num_predict",req.kind==ReplyKind::Conversation?320:180}}},
        {"messages",messages}
    }).dump();
}
void AIService::launch(Request req){
    active=std::move(req);
    const std::string body=query(model,active,turns,outcomes);
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
        if(reply.kind==ReplyKind::Conversation) {
            const std::string conflict=conflictWithObservedWorld(reply.speech,active.observation);
            if(!conflict.empty()) {
                if(active.retryHint.empty()) {
                    Request corrected=active;
                    corrected.retryHint="CONTRADICTION with real game observations: "+conflict+
                        " Your last speech was: "+reply.speech.substr(0,250)+
                        ". Produce a new factually correct answer without made-up equipment or actions.";
                    launch(std::move(corrected));
                    return std::nullopt;
                }
                // If a second attempt still contradicts sensors, speak a minimal
                // truthful response instead of displaying an impossible event.
                reply.speech=safeGroundedAnswer(active.observation,conflict);
                object["actions"]=json::array();
            }
        }
        if(reply.kind==ReplyKind::Conversation && active.retryHint.empty() && !turns.empty()
           && active.playerText!=turns.back().player
           && repeatsEarlierAnswer(reply.speech,turns.back().officer)) {
            // One bounded retry; never spin indefinitely if the local model repeats.
            Request improved=active;
            improved.retryHint="The previous response was: "+turns.back().officer.substr(0,300)+
                ". Please avoid repeating the earlier answer. Answer the new prisoner message with different concrete, relevant information;"+
                " if the same rule applies, explain a different practical consequence or option.";
            launch(std::move(improved));
            return std::nullopt;
        }
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
            if(turns.size()>24)turns.erase(turns.begin());
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
