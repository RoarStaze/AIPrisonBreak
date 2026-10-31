#include "ai.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
using namespace prison;
using json=nlohmann::json;
static bool pump(AIService& ai,Reply& out) {
    for(int i=0;i<600;i++){
        if(auto r=ai.poll()){out=*r;return true;}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
int main(){
    World w;
    std::promise<void> gate;
    std::shared_future<void> release=gate.get_future().share();
    std::atomic<int> calls{0};
    std::vector<std::string> payloads;
    std::mutex mu;
    AIService ai([&](const std::string& request){
        auto j=json::parse(request);
        {
            std::lock_guard<std::mutex> lock(mu);
            payloads.push_back(request);
        }
        if(++calls==1)release.wait();
        const auto messages=j.at("messages");
        const auto last=messages.back().at("content").get<std::string>();
        std::string speech=last.find("Tell me about the exit")!=std::string::npos?
            "That gate leads to the yard. It stays locked during lockdown.":
            last.find("Can you hear me?")!=std::string::npos?
            "I hear you over the intercom. State your business.":"Patrol observation acknowledged.";
        return json({{"message",{{"content",json({{"speech",speech},{"actions",json::array()}}).dump()}}}}).dump();
    });
    assert(ai.requestPatrol(w));
    assert(ai.submitMessage(w,"Can you hear me?"));
    assert(ai.hasPendingConversation());
    assert(!ai.requestPatrol(w));
    gate.set_value();
    Reply response;
    assert(pump(ai,response));
    assert(response.kind==ReplyKind::Conversation);
    assert(response.speech.find("I hear you")!=std::string::npos);
    assert(ai.history().size()==1);
    assert(ai.history()[0].player=="Can you hear me?");
    assert(ai.history()[0].officer.find("I hear you")!=std::string::npos);
    assert(ai.submitMessage(w,"Tell me about the exit"));
    assert(pump(ai,response));
    assert(response.kind==ReplyKind::Conversation);
    assert(response.speech.find("gate")!=std::string::npos);
    {
        std::lock_guard<std::mutex> lock(mu);
        assert(payloads.size()==3);
        auto second=json::parse(payloads[1]);
        auto third=json::parse(payloads[2]);
        assert(second["messages"].back()["content"].get<std::string>().find("Can you hear me?")!=std::string::npos);
        // Prior turns are supplied to the actual model (not a fake local dialogue).
        std::string thirdText=third["messages"].dump();
        assert(thirdText.find("I hear you over the intercom")!=std::string::npos);
        assert(thirdText.find("Tell me about the exit")!=std::string::npos);
    }
    // Failed transport must return a visible error rather than silently losing chat.
    AIService disconnected([](const std::string&)->std::string{throw std::runtime_error("Ollama offline");});
    assert(disconnected.submitMessage(w,"Hello?"));
    assert(pump(disconnected,response));
    assert(response.kind==ReplyKind::Conversation);
    assert(!response.error.empty());
    assert(disconnected.status.find("Ollama offline")!=std::string::npos);
    assert(disconnected.history().empty()); // failed replies are not invented
    // Perception must not imply Voss sees the prisoner through cell walls.
    const auto observation=json::parse(w.observationJson());
    assert(observation["prisonerVisible"]==false);
    assert(observation["lastKnown"].is_null());
    assert(observation["prisonerPosition"].is_null());
    // Stronger agent: report action outcome to the model for course correction.
    AIService reflective([&](const std::string& request){
        const auto j=json::parse(request);
        const auto text=j["messages"].back()["content"].get<std::string>();
        assert(text.find("door was blocked by the lockdown")!=std::string::npos);
        assert(j["options"]["num_ctx"].get<int>()>=8192);
        assert(j["options"]["repeat_penalty"].get<double>()>1.0);
        assert(j["model"]=="qwen3:8b");
        return json({{"message",{{"content",json({{"speech",""},{"actions",json::array()}}).dump()}}}}).dump();
    });
    reflective.model="qwen3:8b";
    reflective.noteOutcome("Officer tried opening the security door; door was blocked by the lockdown");
    assert(reflective.requestPatrol(w));
    assert(pump(reflective,response));
    assert(response.kind==ReplyKind::Patrol);

    // Detect virtually identical boilerplate after different prisoner questions.
    int responseCalls=0;
    AIService varied([&](const std::string& request){
        ++responseCalls;
        const auto j=json::parse(request);
        const auto message=j["messages"].back()["content"].get<std::string>();
        std::string spoken;
        if(responseCalls==1)spoken="Stay in your cell. This is an active lockdown.";
        else if(responseCalls==2)spoken="Stay in your cell; this is an active lockdown!";
        else {
            assert(message.find("avoid repeating")!=std::string::npos);
            spoken="There is a medical officer on shift. I can request a wellness check.";
        }
        return json({{"message",{{"content",json({{"speech",spoken},{"actions",json::array()}}).dump()}}}}).dump();
    });
    assert(varied.submitMessage(w,"What is happening?"));
    assert(pump(varied,response));
    assert(varied.submitMessage(w,"I feel sick, can I see someone?"));
    assert(pump(varied,response));
    assert(responseCalls==3);
    assert(response.speech.find("medical officer")!=std::string::npos);
    assert(varied.history().size()==2);

    // Conversation memory keeps more than eight turns; no hard cutoff at 8.
    AIService memory([](const std::string&)->std::string{
        return json({{"message",{{"content",json({{"speech","Acknowledged."},{"actions",json::array()}}).dump()}}}}).dump();
    });
    for(int i=0;i<12;i++){
        assert(memory.submitMessage(w,"Question "+std::to_string(i)));
        assert(pump(memory,response));
    }
    assert(memory.history().size()>=12);
    std::cout<<"AI conversation priority, context, anti-repetition, action feedback and perception tests passed\n";
}
