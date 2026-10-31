#pragma once
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace prison {
struct Point { float x=0, z=0; };
float distance(Point a, Point b);
struct Door { std::string id; int x,z; bool open=false, locked=true, card=false; };
struct Object { std::string id, kind; Point pos; bool taken=false; };
struct Event { float time; std::string message; Point source; float loudness=0; };
struct Actor {
    Point pos; Point destination; float angle=0, stamina=1;
    bool crouching=false, moving=false, spotted=false;
};
struct Observation {
    bool playerVisible=false, playerAudible=false;
    Point playerLastKnown{};
    std::vector<std::string> visibleObjects, reports, sounds;
};
struct ActionResult { bool ok=false; std::string text; };
class World {
public:
    static constexpr int width=28, height=20;
    World();
    Actor player{{5.5f,5.5f}}, guard{{14.5f,9.5f}};
    std::vector<Door> doors;
    std::vector<Object> objects;
    bool lockdown=true, cameras=true, lights=true, escaped=false, caught=false;
    bool hasHairpin=false, hasCard=false;
    int alert=1;
    float elapsed=0, noise=0, lastSeen=-100;
    std::string guardSpeech="Keep moving. Nothing to see here.", status="Find a way out.";
    std::deque<Event> events;
    bool solid(int x,int z) const;
    bool canStand(Point pos, float radius=.22f) const;
    bool lineOfSight(Point a, Point b) const;
    void movePlayer(Point delta, bool crouch, bool run);
    void update(float dt);
    Observation perceive() const;
    ActionResult command(const std::string& action,const std::string& target="",Point at={});
    std::string interactionHint() const;
    ActionResult playerInteract();
    std::string observationJson() const;
    std::string stateJson() const;
    void log(std::string message,Point source={},float loudness=0);
    const Door* doorAt(int x,int z) const;
private:
    Point lastKnown{5.5f,5.5f};
    void followGuard(float dt);
    bool tryMove(Actor& actor, Point destination, float step);
    std::optional<Point> nextPathPoint(Point from,Point to) const;
};
}
