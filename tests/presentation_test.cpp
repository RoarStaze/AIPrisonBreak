#include "presentation_math.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace prison;
    float yaw=0.f;
    yaw=applyMouseYaw(yaw,100.f,0.00225f);
    assert(yaw>0.f); // moving mouse right looks right, never inverted
    auto f=heading(yaw);
    assert(f.x>0.f && f.z<0.f);
    const auto shut=doorVisual(10,6,false);
    assert(std::abs(shut.centerX-10.5f)<.001f);
    assert(std::abs(shut.centerZ-6.5f)<.001f);
    assert(shut.depthZ>.75f && shut.depthX<.2f); // door spans Z across wall running along Z
    const auto open=doorVisual(10,6,true);
    assert(open.depthX>.7f && open.depthZ<.2f);
    assert(open.centerX>shut.centerX); // visually parked beyond threshold
    assert(open.centerZ<shut.centerZ); // hinged at edge of opening
    std::cout << "First-person yaw and door geometry assertions passed.\n";
}
