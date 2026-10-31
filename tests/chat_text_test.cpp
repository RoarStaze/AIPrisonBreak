#include "chat_text.hpp"
#include "chat_ui_layout.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace prison;
int main() {
    auto count=[](const std::string& s)->float{return static_cast<float>(s.size());};
    const std::string spoken="I can't give you a keycard. This is an active lockdown. You're not authorized to leave. Stay in your cell and await instructions.";
    auto lines=wrapChatText(spoken,32.f,count);
    assert(lines.size()>2);
    for(const auto& line:lines)assert(count(line)<=32.f);
    std::string restored;
    for(const auto& line:lines){if(!restored.empty())restored+=" ";restored+=line;}
    assert(restored==spoken); // No 78/110/240-character truncation anywhere in layout
    assert(wrapChatText("First line\nSecond line",30.f,count).size()==2);
    auto tight=wrapChatText("longunbrokentext",5.f,count);
    assert(tight.size()>=3);
    std::string reconstructed;
    for(const auto& line:tight)reconstructed+=line;
    assert(reconstructed=="longunbrokentext");
    std::string input;
    appendChatInput(input,{'t'},true);
    assert(input.empty()); // opening T is consumed, not inserted
    appendChatInput(input,{'h','i',' ','t'},false);
    assert(input=="hi t"); // typing T in an open chat is still allowed
    appendChatInput(input,{'a','b','c'},false,5);
    assert(input=="hi ta"); // never overrun the input capacity
    // UX regression: keep the original compact bottom composer, not a fullscreen dialog.
    const auto small=compactChatLayout(1280,720,0.8f,190.f);
    assert(small.composerWidth<=920.f*.8f+0.01f);
    assert(small.composerHeight==164.f*.8f);
    assert(small.composerY>720.f*.57f);
    assert(small.historyY+small.historyHeight<small.composerY);
    assert(small.historyWidth==small.composerWidth);
    assert(small.historyAlpha>=145 && small.historyAlpha<=215);
    const auto large=compactChatLayout(2560,1080,1.2f,900.f);
    assert(large.composerWidth<=920.f*1.2f+0.01f);
    assert(large.composerY>=1080.f*.50f);
    assert(large.historyY>=10.f);
    assert(large.historyY+large.historyHeight<large.composerY);
    std::cout<<"Wrapped dialogue, input hotkey, and compact HUD layout tests passed.\n";
}
