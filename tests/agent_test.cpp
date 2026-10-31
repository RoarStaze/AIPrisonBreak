#include "agent.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace prison;
int main(){
    World w;
    AgentExecutor e;
    assert(!e.active()&&e.remaining()==0);
    // An action chain must not interact until the NPC physically reaches the device.
    w.guard.pos={20.5f,4.5f};
    w.guard.destination={20.5f,4.5f};
    e.setPlan({{"move","",{21.5f,4.5f}},{"interact","security_terminal",{}}});
    assert(e.remaining()==2);
    assert(w.cameras);
    e.tick(w,.016f);
    assert(e.active()&&w.cameras); // First step is still en route.
    for(int i=0;i<180 && e.active();++i){w.update(1.f/60.f);e.tick(w,1.f/60.f);}
    assert(!e.active());
    assert(!w.cameras); // Device interaction occurs only after movement.
    e.setPlan({{"dance","",{}},{"alarm","3",{}}});
    e.tick(w,1.f/60.f);
    assert(!e.active()&&w.alert==3); // Invalid action cannot block valid follow-up.
    e.setPlan({{"move","",{0,0}},{"alarm","1",{}}});
    e.tick(w,1.f/60.f);
    assert(!e.active()&&w.alert==1); // Blocked destinations are skipped.
    e.setPlan({{"alarm","2",{}},{"alarm","3",{}},{"lights","off",{}},{"cameras","on",{}}});
    assert(e.remaining()==3); // Defensively cap generated multi-action plans.
    e.clear();assert(!e.active());
    std::cout<<"Agent multi-step plan tests passed.\n";
}
