#pragma once
#include <algorithm>
namespace prison {
struct CompactChatLayout {
    float composerX=0,composerY=0,composerWidth=0,composerHeight=0;
    float historyX=0,historyY=0,historyWidth=0,historyHeight=0;
    int historyAlpha=190;
};
// Preserve the original bottom-chat placement and constrain transcript to the
// area immediately above the composer. Never turn the chat into a full-screen UI.
inline CompactChatLayout compactChatLayout(int sw,int sh,float scale,float requestedHistoryHeight){
    CompactChatLayout r{};
    scale=std::max(.5f,scale);
    r.composerWidth=std::min(sw-50.f*scale,920.f*scale);
    r.composerWidth=std::max(1.f,r.composerWidth);
    r.composerHeight=164.f*scale;
    r.composerX=(sw-r.composerWidth)*.5f;
    r.composerY=sh-335.f*scale;
    r.historyWidth=r.composerWidth;
    r.historyX=r.composerX;
    float maxHeight=std::max(0.f,std::min(240.f*scale,r.composerY-20.f*scale));
    r.historyHeight=std::clamp(requestedHistoryHeight,0.f,maxHeight);
    r.historyY=r.composerY-11.f*scale-r.historyHeight;
    return r;
}
}
