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
    std::cout<<"AI conversation priority, context, error and perception tests passed\n";
}
