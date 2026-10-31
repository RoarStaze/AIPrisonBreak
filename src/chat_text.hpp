#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace prison {
// Layout owns visual line splitting. It must not truncate the source dialogue.
// Callback uses the currently loaded native font, so pixel widths are exact.
using ChatMeasure = std::function<float(const std::string&)>;

inline std::vector<std::string> wrapChatText(const std::string& source,float maxWidth,const ChatMeasure& measure) {
    std::vector<std::string> lines;
    std::string line;
    if(maxWidth<=0)maxWidth=1.f;
    auto push=[&](){lines.push_back(line);line.clear();};
    auto fitWord=[&](const std::string& word){
        if(word.empty())return;
        std::string candidate=line.empty()?word:(line+" "+word);
        if(measure(candidate)<=maxWidth){line=std::move(candidate);return;}
        if(!line.empty())push();
        if(measure(word)<=maxWidth){line=word;return;}
        // Split an overlong word at UTF-8 code point boundaries rather than bytes.
        std::string part;
        for(size_t p=0;p<word.size();){
            const unsigned char c=static_cast<unsigned char>(word[p]);
            size_t advance=c<0x80?1:(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:(c&0xF8)==0xF0?4:1;
            advance=std::min(advance,word.size()-p);
            auto cp=word.substr(p,advance);
            if(!part.empty()&&measure(part+cp)>maxWidth){lines.push_back(part);part.clear();}
            part+=cp;p+=advance;
        }
        line=part;
    };
    std::string word;
    for(char ch:source) {
        if(ch=='\n'){
            fitWord(word);word.clear();push();
        } else if(ch==' '||ch=='\t'||ch=='\r'){
            fitWord(word);word.clear();
        } else word+=ch;
    }
    fitWord(word);
    if(!line.empty()||lines.empty())lines.push_back(line);
    return lines;
}
// Opening the chat must consume the T key's typed-character event.
// Later typing a literal "t" into the open chat remains valid.
inline void appendChatInput(std::string& input,const std::vector<int>& typed,bool openedThisFrame,
                            std::size_t maxLength=180) {
    if(openedThisFrame)return;
    for(int code:typed)if(code>=32&&code<=126&&input.size()<maxLength)input.push_back(static_cast<char>(code));
}
}
