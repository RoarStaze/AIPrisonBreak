#pragma once
#include "world.hpp"
#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <vector>
namespace prison {
struct Decision { std::string action,target; Point at{}; };
struct Reply {std::vector<Decision> actions; std::string speech;};
class AIService {
public:
    std::string model="qwen3:4b-instruct";
    std::string status="OLLAMA: CHECKING";
    bool busy() const;
    bool request(const World& world,const std::string& playerMessage="");
    std::optional<Reply> poll();
private:
    std::future<std::string> pending;
    static std::string query(const std::string& model,const std::string& observation,const std::string& playerMessage);
};
}
