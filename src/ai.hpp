#pragma once
#include "world.hpp"
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>
namespace prison {
struct Decision {std::string action,target; Point at{};};
enum class ReplyKind {Patrol,Conversation};
struct Reply {
    std::vector<Decision> actions;
    std::string speech;
    std::string error;
    ReplyKind kind=ReplyKind::Patrol;
};
struct DialogueTurn {std::string player,officer;};
class AIService {
public:
    using Transport=std::function<std::string(const std::string&)>;
    explicit AIService(Transport transport={});
    std::string model="qwen3.5:4b-q4_K_M";
    std::string status="OLLAMA: READY";
    bool busy() const;
    bool hasPendingConversation() const;
    void noteOutcome(const std::string& verifiedResult);
    bool requestPatrol(const World& world);
    bool submitMessage(const World& world,const std::string& message);
    std::optional<Reply> poll();
    const std::vector<DialogueTurn>& history() const{return turns;}
private:
    struct Request {ReplyKind kind; std::string observation,playerText,retryHint;};
    std::deque<Request> waiting;
    std::vector<DialogueTurn> turns;
    std::deque<std::string> outcomes;
    Transport transport;
    std::future<std::string> pending;
    Request active{ReplyKind::Patrol,"",""};
    void launch(Request request);
    static std::string query(const std::string& model,const Request& request,const std::vector<DialogueTurn>& history,
                             const std::deque<std::string>& outcomes);
};
}
