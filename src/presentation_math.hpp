#pragma once
#include "world.hpp"
#include <cmath>
namespace prison {
// Rightward mouse motion increases yaw: forward points toward +X.
inline float applyMouseYaw(float yaw, float deltaX, float sensitivity=0.00225f) {
    return yaw + deltaX*sensitivity;
}
inline Point heading(float yaw) { return {std::sin(yaw),-std::cos(yaw)}; }
inline Point strafeRight(float yaw) { return {std::cos(yaw), std::sin(yaw)}; }

// Doors occupy an x-normal wall. Closed: narrow X, spans the Z opening.
// Open: hinge at the low-Z jamb, leaf lies along +X, OUT of the walkway.
struct DoorVisual {
    float centerX, centerZ, depthX, depthZ;
};
inline DoorVisual doorVisual(int tileX,int tileZ,bool opened) {
    const float x=tileX+.5f,z=tileZ+.5f;
    if(opened) return {x+.44f,z-.40f,.84f,.095f};
    return {x,z,.105f,.84f};
}
}
