#pragma once
#include "ai.hpp"
#include <deque>
#include <string>
#include <vector>

namespace prison {
// Applies language-model plans through the authoritative game simulation,
// waiting for physical movement to complete before executing the next step.
class AgentExecutor {
public:
    void setPlan(const std::vector<Decision>& actions);
    void clear();
    bool active() const;
    size_t remaining() const;
    void tick(World& world, float dt);
    std::string description() const;
    std::string lastEvent="Awaiting a plan";
private:
    std::deque<Decision> pending;
    bool issued=false;
    float duration=0;
};
}
