#include "chat_text.hpp"
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
    std::cout<<"Wrapped dialogue and consumed opening key tests passed.\n";
}
