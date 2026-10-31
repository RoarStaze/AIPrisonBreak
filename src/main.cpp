#include "raylib.h"
#include "raymath.h"
#include "world.hpp"
#include "ai.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace prison;
static constexpr float PI2=6.283185307f;
static Color rgba(int r,int g,int b,int a=255){return {(unsigned char)r,(unsigned char)g,(unsigned char)b,(unsigned char)a};}
static Vector3 v3(float x,float y,float z){return {x,y,z};}
static unsigned hash(unsigned n){n=(n^61u)^(n>>16);n*=9u;n^=n>>4;n*=0x27d4eb2du;n^=n>>15;return n;}
static Color textureColor(int x,int y,int seed,bool metal){
    unsigned h=hash((unsigned)(x/3*92837111u+y/3*689287499u+seed*283u));
    int noise=(int)(h%27)-13;
    int grain=(int)(hash((unsigned)(x*39+y*97+seed))%11)-5;
    int value=(metal?102:113)+noise+grain;
    if(!metal && ((x%64==0)||(y%64==0)))value-=28;
    if(metal && (x%43==0))value+=24;
    if((hash((unsigned)(x/19+y/17+seed))%101)==0)value-=21;
    return rgba(std::clamp(value+(metal?1:4),0,255),std::clamp(value+(metal?10:7),0,255),std::clamp(value+(metal?15:7),0,255));
}
static Texture2D makeTexture(int seed,bool metal){
    Image img=GenImageColor(256,256,WHITE);
    auto* pix=static_cast<Color*>(img.data);
    for(int y=0;y<256;y++)for(int x=0;x<256;x++)pix[y*256+x]=textureColor(x,y,seed,metal);
    Texture2D t=LoadTextureFromImage(img);UnloadImage(img);
    SetTextureFilter(t,TEXTURE_FILTER_BILINEAR);return t;
}
static void box(float x,float y,float z,float sx,float sy,float sz,Color color){
    DrawCube(v3(x,y,z),sx,sy,sz,color);
}
static void beam(Vector3 a,Vector3 b,float radius,Color color,int sides=10){
    DrawCylinderEx(a,b,radius,radius,sides,color);
}
static void warningLight(float x,float y,float z,float seconds,Color color){
    bool flashing=(int)(seconds*2)%2==0;
    DrawSphere(v3(x,y,z),.12f,flashing?color:rgba(54,27,24));
    DrawSphereWires(v3(x,y,z),.145f,6,6,rgba(123,130,128));
}
// Reusable textured mesh replaces raylib example-only DrawCubeTexture helper.
static Model gSurfaceCube{};
static void texturedBox(Texture2D texture,Vector3 pos,float sx,float sy,float sz,Color tint){
    if(gSurfaceCube.meshCount==0)gSurfaceCube=LoadModelFromMesh(GenMeshCube(1,1,1));
    gSurfaceCube.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=texture;
    DrawModelEx(gSurfaceCube,pos,v3(0,1,0),0,v3(sx,sy,sz),tint);
}
static void drawDoor(const Door& d){
    float z=d.z+.5f,x=d.x+.5f;
    if(d.open){
        box(x-.43f,1.38f,z,.09f,2.74f,.17f,rgba(32,46,52));
        box(x+.43f,1.38f,z,.09f,2.74f,.17f,rgba(32,46,52));
        box(x,2.72f,z,.93f,.11f,.17f,rgba(27,34,41));
    }else {
        box(x,1.36f,z,.94f,2.68f,.14f,rgba(33,49,58));
        box(x,1.4f,z+.083f,.79f,2.47f,.012f,rgba(57,74,83));
        box(x,1.4f,z-.083f,.79f,2.47f,.012f,rgba(47,64,72));
        box(x,1.88f,z+.093f,.52f,.64f,.02f,rgba(18,28,31));
        box(x,1.88f,z+.11f,.47f,.57f,.012f,rgba(92,108,105,170));
        box(x+.32f,1.11f,z+.11f,.038f,.24f,.035f,rgba(175,184,174));
    }
    box(x-.5f,1.37f,z,.06f,2.74f,.24f,rgba(17,22,27));
    box(x+.5f,1.37f,z,.06f,2.74f,.24f,rgba(17,22,27));
    Color access=d.locked?rgba(201,64,49):rgba(85,194,142);
    box(x+.58f,1.45f,z+.15f,.13f,.3f,.06f,rgba(12,23,28));
    box(x+.59f,1.45f,z+.195f,.055f,.065f,.018f,access);
}
static void drawGuard(Point p,float angle,float time,bool alert){
    // Articulated rounded anthropomorphic placeholder; character can later be replaced by a rigged asset.
    Vector3 base=v3(p.x,0,p.z);
    float sway=std::sin(time*6.f)*.085f;
    DrawCylinder(v3(p.x,.009f,p.z),.42f,.42f,.012f,20,rgba(0,0,0,92));
    Color pants=rgba(24,31,40),shirt=rgba(34,46,54),skin=rgba(183,143,118);
    beam(v3(p.x-.16f,.77f,p.z),v3(p.x-.17f+sway*.5f,.12f,p.z+.08f),.115f,pants);
    beam(v3(p.x+.16f,.77f,p.z),v3(p.x+.17f-sway*.5f,.12f,p.z-.08f),.115f,pants);
    box(p.x-.17f,.13f,p.z-.06f,.22f,.16f,.39f,rgba(18,20,25));
    box(p.x+.17f,.13f,p.z-.06f,.22f,.16f,.39f,rgba(18,20,25));
    DrawCylinderEx(v3(p.x,.82f,p.z),v3(p.x,1.64f,p.z),.28f,.33f,16,shirt);
    box(p.x,1.28f,p.z+.29f,.32f,.25f,.065f,rgba(52,60,67));
    box(p.x,1.25f,p.z+.34f,.09f,.18f,.025f,rgba(192,168,90));
    beam(v3(p.x-.34f,1.5f,p.z),v3(p.x-.48f,1.01f,p.z+.05f),.105f,shirt);
    beam(v3(p.x+.34f,1.5f,p.z),v3(p.x+.48f,1.01f,p.z-.04f),.105f,shirt);
    DrawSphere(v3(p.x-.48f,1.02f,p.z+.05f),.105f,skin);
    DrawSphere(v3(p.x+.48f,1.02f,p.z-.04f),.105f,skin);
    DrawSphere(v3(p.x,1.86f,p.z),.235f,skin);
    DrawSphere(v3(p.x,2.038f,p.z-.028f),.24f,rgba(31,35,39));
    box(p.x,2.037f,p.z+.16f,.52f,.07f,.22f,rgba(29,35,42));
    DrawSphere(v3(p.x,1.87f,p.z+.205f),.08f,rgba(36,45,50));
    if(alert)warningLight(p.x,2.15f,p.z,time,rgba(238,65,49));
    // Direction indicator shows the agent's physical orientation.
    DrawLine3D(v3(p.x,1.8f,p.z),v3(p.x+std::sin(angle)*.43f,1.8f,p.z-std::cos(angle)*.43f),rgba(223,193,119,130));
}
static void worldGeometry(const World& w,Texture2D concrete,Texture2D steel,float time) {
    // Deliberate material variation, surface detailing and repeated environmental storytelling.
    for(int z=1;z<World::height-1;z++)for(int x=1;x<World::width-1;x++){
        Color tint=((x+z)%4==0)?rgba(177,187,184):rgba(151,161,161);
        texturedBox(concrete,v3(x+.5f,-.09f,z+.5f),1,.16f,1,tint);
        if((x+z*3)%5==0)box(x+.5f,-.002f,z+.5f,.02f,.003f,.72f,rgba(45,53,57,130));
    }
    for(int z=0;z<World::height;z++)for(int x=0;x<World::width;x++) {
        if(!w.solid(x,z))continue;
        const Door* d=w.doorAt(x,z);
        if(d)continue;
        bool exposed=!w.solid(x+1,z)||!w.solid(x-1,z)||!w.solid(x,z+1)||!w.solid(x,z-1);
        if(!exposed)continue;
        texturedBox(concrete,v3(x+.5f,1.57f,z+.5f),1,3.16f,1,rgba(134,145,146));
        box(x+.5f,.18f,z+.5f,1,.36f,1,rgba(38,47,52));
        box(x+.5f,3.04f,z+.5f,1,.24f,1,rgba(51,63,68));
        if((z+x*2)%3==0)box(x+.5f,1.6f,z+.5f,.012f,2.2f,.025f,rgba(39,46,51,95));
    }
    for(const auto& d:w.doors)drawDoor(d);
    // Ceiling beams and lights: not merely a single uniform flat plane.
    for(int x=2;x<World::width-1;x+=4){
        box(x+.5f,3.21f,9.5f,.13f,.26f,16.3f,rgba(43,54,61));
        box(x+.5f,3.21f,16.4f,.14f,.23f,4.7f,rgba(43,54,61));
    }
    for(int z=3;z<World::height-1;z+=5){
        box(5.2f,3.04f,z+.5f,1.18f,.08f,.42f,rgba(196,205,192));
        box(14.1f,3.04f,z+.5f,1.18f,.08f,.42f,rgba(196,205,192));
        if(w.lights){
            box(5.2f,3.002f,z+.5f,1.07f,.01f,.32f,rgba(241,235,204));
            box(14.1f,3.002f,z+.5f,1.07f,.01f,.32f,rgba(241,235,204));
        }
    }
    for(int z=2;z<World::height-1;z+=4){
        beam(v3(9.75f,2.86f,z),v3(9.75f,2.86f,z+3.7f),.055f,rgba(84,95,97));
        beam(v3(18.24f,2.86f,z),v3(18.24f,2.86f,z+3.7f),.046f,rgba(82,90,99));
    }
    // Beds and metal furniture in the cells.
    for(int z: {3,8,13}){
        box(3.22f,.42f,z+.55f,2.0f,.13f,.93f,rgba(54,63,69));
        box(3.22f,.515f,z+.55f,1.91f,.12f,.84f,rgba(100,112,112));
        box(2.68f,.59f,z+.55f,.65f,.1f,.7f,rgba(170,177,165));
        for(float x:{2.3f,4.14f})for(float zz:{z+.17f,z+.98f})beam(v3(x,.08f,zz),v3(x,.45f,zz),.045f,rgba(47,55,58));
        box(7.85f,.85f,z+.2f,.85f,1.7f,.72f,rgba(56,67,72));
        for(int n=0;n<3;n++)box(8.29f,.54f+n*.38f,z+.2f,.015f,.22f,.48f,rgba(38,48,53));
    }
    // Pipes, wall marks, signs and industrial equipment.
    for(int z=2;z<18;z+=3){
        beam(v3(1.55f,2.8f,z),v3(1.55f,2.8f,z+2.65f),.045f,rgba(82,102,106));
        box(1.4f,2.8f,z+1.3f,.035f,.18f,.23f,rgba(44,51,55));
    }
    for(int n=0;n<5;n++){
        box(11.25f+n*1.3f,2.36f,1.03f,.86f,.54f,.05f,rgba(32,37,40));
        for(int v=0;v<5;v++)box(10.9f+n*1.3f+v*.17f,2.36f,1.063f,.05f,.42f,.012f,rgba(109,117,116));
    }
    // CCTV security cameras on articulated mounts.
    for(Point c: {Point{13.5f,3.5f},Point{22.5f,15.5f}}){
        beam(v3(c.x,3.05f,c.z),v3(c.x,2.73f,c.z),.055f,rgba(105,112,116));
        box(c.x,2.65f,c.z,.37f,.19f,.38f,rgba(40,49,54));
        DrawSphere(v3(c.x,2.62f,c.z+.215f),.11f,rgba(15,22,29));
        if(w.cameras)warningLight(c.x+.2f,2.7f,c.z,time,rgba(239,51,42));
    }
    // Terminal and generator.
    box(21.5f,.72f,4.5f,1.3f,1.44f,.67f,rgba(43,53,60));
    box(21.5f,1.21f,4.86f,.91f,.52f,.06f,w.cameras?rgba(17,120,119):rgba(87,45,40));
    for(int n=0;n<5;n++)box(21.2f+n*.14f,1.05f,4.902f,.085f,.015f,.008f,rgba(134,209,182));
    box(22.5f,.88f,13.5f,1.5f,1.76f,.8f,rgba(69,78,72));
    for(int k=0;k<5;k++)beam(v3(21.92f,1.12f+k*.15f,13.91f),v3(23.08f,1.12f+k*.15f,13.91f),.025f,rgba(31,35,38));
    warningLight(22.5f,1.84f,13.5f,time,rgba(229,145,74));
    for(const auto& o:w.objects)if(!o.taken&&o.kind=="item"){
        if(o.id=="hairpin")DrawSphere(v3(o.pos.x,.55f,o.pos.z),.07f,rgba(209,179,104));
        else{
            box(o.pos.x,.9f,o.pos.z,.28f,.08f,.18f,rgba(205,160,75));
            box(o.pos.x,.945f,o.pos.z,.18f,.01f,.07f,rgba(48,68,70));
        }
    }
}
static Font interfaceFont(){
#ifdef _WIN32
    Font f=LoadFontEx("C:/Windows/Fonts/segoeui.ttf",28,nullptr,0);
    if(f.texture.id)return f;
#endif
    return GetFontDefault();
}
static void text(Font font,const std::string& str,float x,float y,float size,Color color,float spacing=1.2f){
    DrawTextEx(font,str.c_str(),{x,y},size,spacing,color);
}
static void panel(Rectangle rec,Color c){
    DrawRectangleRounded(rec,.12f,6,c);
    DrawRectangleRoundedLinesEx(rec,.12f,6,1.0f,rgba(114,151,155,85));
}
static void bar(float x,float y,float width,float value,Color color){
    DrawRectangleRounded({x,y,width,5},.5f,4,rgba(60,79,86,170));
    DrawRectangleRounded({x,y,width*std::clamp(value,0.f,1.f),5},.5f,4,color);
}
static void minimap(const World& w,int screenW,int screenH){
    const float scale=5.3f, x=(float)screenW-204.f, y=(float)screenH-171.f;
    DrawRectangleRounded({x-13,y-13,174,137},.04f,3,rgba(8,18,24,222));
    for(int z=0;z<World::height;z++)for(int xx=0;xx<World::width;xx++) {
        auto* d=w.doorAt(xx,z);
        Color c=d?(d->open?rgba(92,150,116):rgba(177,111,66)):(w.solid(xx,z)?rgba(75,91,95):rgba(26,41,47));
        DrawRectangle((int)(x+xx*scale),(int)(y+z*scale),5,5,c);
    }
    DrawCircle((int)(x+w.player.pos.x*scale),(int)(y+w.player.pos.z*scale),3.7f,rgba(141,231,201));
    DrawCircle((int)(x+w.guard.pos.x*scale),(int)(y+w.guard.pos.z*scale),3.5f,rgba(238,91,83));
}
static void overlay(const World& w,const AIService& ai,Font font,bool chat,const std::string& input,bool pause){
    int sw=GetScreenWidth(),sh=GetScreenHeight();
    DrawRectangleGradientV(0,0,sw,142,rgba(4,11,17,205),rgba(4,11,17,0));
    text(font,"BLACKWATER  /  C-BLOCK",32,20,16,rgba(149,203,208));
    text(font,w.lockdown?"LOCKDOWN   ACTIVE":"SECURITY  SYSTEM  NORMAL",31,51,29,w.lockdown?rgba(236,104,89):rgba(126,220,183));
    text(font,"YOU ARE IN RESTRICTED CUSTODY",32,88,14,rgba(185,196,189));
    panel({(float)sw-335,22,302,61},rgba(5,17,24,205));
    text(font,"OFFICER VOSS / AUTONOMOUS AGENT",sw-321.f,33,13,rgba(145,184,190));
    text(font,ai.status.substr(0,37),sw-321.f,54,13,rgba(231,169,95));
    DrawRectangleGradientV(0,sh-165,sw,165,rgba(4,11,18,0),rgba(3,8,13,225));
    text(font,w.status,30,(float)sh-123,19,rgba(231,224,205));
    text(font,"WASD MOVE    SHIFT SPRINT    C CROUCH    E INTERACT    T TALK    F FLASHLIGHT    ESC PAUSE",30,(float)sh-69,13,rgba(172,193,193));
    text(font,"STAMINA",30,(float)sh-46,12,rgba(181,193,187));
    bar(105,(float)sh-41,160,w.player.stamina,rgba(105,216,185));
    text(font,"ALERT  "+std::to_string(w.alert)+" / 3",296,(float)sh-46,12,w.alert>1?rgba(241,102,94):rgba(205,192,138));
    text(font,w.hasHairpin?"HAIRPIN +":"HAIRPIN -",400,(float)sh-46,12,rgba(198,200,179));
    text(font,w.hasCard?"CARD +":"CARD -",513,(float)sh-46,12,rgba(198,200,179));
    minimap(w,sw,sh);
    int cx=sw/2,cy=sh/2;
    DrawLine(cx-7,cy,cx-2,cy,rgba(205,224,218,195));
    DrawLine(cx+2,cy,cx+7,cy,rgba(205,224,218,195));
    DrawLine(cx,cy-7,cx,cy-2,rgba(205,224,218,195));
    DrawLine(cx,cy+2,cx,cy+7,rgba(205,224,218,195));
    panel({30,(float)sh-236,std::min(620.f,(float)sw-60),61},rgba(6,17,24,192));
    text(font,"VOSS: "+w.guardSpeech.substr(0,77),43,(float)sh-216,16,rgba(217,226,221));
    if(chat) {
        Rectangle r={sw*.15f,sh*.68f,sw*.7f,105};
        panel(r,rgba(5,16,23,244));
        text(font,"SPEAK TO OFFICER VOSS  /  ENTER TO SEND  /  ESC TO CANCEL",r.x+20,r.y+14,15,rgba(159,212,214));
        text(font,"> "+input+"_",r.x+20,r.y+52,23,rgba(241,237,218));
    }
    if(pause) {
        DrawRectangle(0,0,sw,sh,rgba(0,0,0,180));
        panel({sw*.5f-260.f,sh*.5f-136.f,520,272},rgba(11,25,34,245));
        text(font,"GAME PAUSED",sw*.5f-105,sh*.5f-87,30,rgba(235,237,220));
        text(font,"ESC  RESUME     F11  TOGGLE FULLSCREEN",sw*.5f-194,sh*.5f-22,17,rgba(182,211,212));
        text(font,"R  RESTART SESSION",sw*.5f-124,sh*.5f+30,17,rgba(182,211,212));
    }
    if(w.caught||w.escaped){
        DrawRectangle(0,0,sw,sh,rgba(0,0,0,185));
        std::string headline=w.escaped?"YOU ESCAPED":"YOU WERE DETAINED";
        text(font,headline,sw*.5f-200,sh*.5f-80,40,w.escaped?rgba(127,219,177):rgba(236,92,83));
        text(font,"PRESS R TO START A NEW SESSION",sw*.5f-159,sh*.5f+15,18,rgba(234,234,217));
    }
}
int main(){
    SetConfigFlags(FLAG_MSAA_4X_HINT|FLAG_VSYNC_HINT|FLAG_FULLSCREEN_MODE);
    InitWindow(1600,900,"AI PRISON BREAK  |  BLACKWATER");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    DisableCursor();
    Font font=interfaceFont();
    Texture2D concrete=makeTexture(53,false);
    Texture2D steel=makeTexture(23,true);
    World w;
    AIService ai;
    float yaw=1.45f,pitch=0.f,worldTick=0.f,brainTick=0.f;
    bool paused=false,chat=false,flashlight=true;
    std::string input;
    while(!WindowShouldClose()){
        float dt=std::min(.05f,GetFrameTime());
        int sw=GetScreenWidth(),sh=GetScreenHeight();
        if(IsKeyPressed(KEY_F11))ToggleFullscreen();
        if(IsKeyPressed(KEY_ESCAPE)){
            if(chat){chat=false;input.clear();DisableCursor();}
            else {paused=!paused;if(paused)EnableCursor();else DisableCursor();}
        }
        if(IsKeyPressed(KEY_R)&&(paused||w.caught||w.escaped)){
            w=World{};yaw=1.45f;pitch=0;worldTick=0;brainTick=0;
            paused=false;chat=false;input.clear();DisableCursor();
        }
        if(!paused&&!w.caught&&!w.escaped){
            if(IsKeyPressed(KEY_T)&&!chat){chat=true;EnableCursor();}
            if(chat){
                for(int c=GetCharPressed();c!=0;c=GetCharPressed())if(c>=32&&c<=126&&input.size()<180)input.push_back((char)c);
                if(IsKeyPressed(KEY_BACKSPACE)&&!input.empty())input.pop_back();
                if(IsKeyPressed(KEY_ENTER)){
                    if(!input.empty()&&ai.request(w,input)){w.log("Prisoner: "+input,w.player.pos,1);w.status="WAITING FOR OFFICER VOSS";input.clear();chat=false;DisableCursor();}
                    else w.status="AI BUSY OR NO MESSAGE";
                }
            }else{
                Vector2 mouse=GetMouseDelta();yaw-=mouse.x*.00225f;pitch=std::clamp(pitch-mouse.y*.00225f,-1.34f,1.34f);
                if(IsKeyPressed(KEY_F))flashlight=!flashlight;
                if(IsKeyPressed(KEY_E)){
                    ActionResult response=w.playerInteract();w.status=response.text;
                }
                float dx=0,dz=0;
                if(IsKeyDown(KEY_W))dz+=1;
                if(IsKeyDown(KEY_S))dz-=1;
                if(IsKeyDown(KEY_A))dx-=1;
                if(IsKeyDown(KEY_D))dx+=1;
                Point vec{dx*std::cos(yaw)+dz*std::sin(yaw), dx*std::sin(yaw)-dz*std::cos(yaw)};
                worldTick+=dt;
                while(worldTick>=1.f/60){w.movePlayer(vec,IsKeyDown(KEY_C)||IsKeyDown(KEY_LEFT_CONTROL),IsKeyDown(KEY_LEFT_SHIFT));worldTick-=1.f/60;}
                w.player.angle=yaw;
            }
            w.update(dt);
            brainTick+=dt;
            if(brainTick>5.4f&&!ai.busy()&&!chat){ai.request(w);brainTick=0;}
        }
        if(auto reply=ai.poll()){
            if(!reply->speech.empty())w.command("speak",reply->speech);
            for(const auto& d:reply->actions) {
                if(d.action=="speak")continue;
                ActionResult res=w.command(d.action,d.target,d.at);
                if(!res.ok)w.log("AI action rejected: "+d.action+" / "+res.text,w.guard.pos);
            }
        }
        Camera3D camera{};
        float eyeHeight=w.player.crouching?1.17f:1.77f;
        float walkBob=(!paused&&!chat&&w.player.moving)?std::sin(w.elapsed*10.f)*.019f:0.f;
        camera.position=v3(w.player.pos.x,eyeHeight+walkBob,w.player.pos.z);
        Vector3 dir=v3(std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch));
        camera.target=Vector3Add(camera.position,dir);
        camera.up=v3(0,1,0);camera.fovy=74;camera.projection=CAMERA_PERSPECTIVE;
        BeginDrawing();
        ClearBackground(rgba(11,17,23));
        BeginMode3D(camera);
        worldGeometry(w,concrete,steel,w.elapsed);
        drawGuard(w.guard.pos,w.guard.angle,w.elapsed,w.alert>1);
        if(flashlight){
            // Player-controlled flashlight glow / near-cone cue. Dynamic lighting material pass is future work.
            Vector3 impact=Vector3Add(camera.position,Vector3Scale(dir,3.3f));
            DrawSphere(impact,.075f,rgba(205,219,178,30));
        }
        EndMode3D();
        overlay(w,ai,font,chat,input,paused);
        EndDrawing();
    }
    UnloadTexture(concrete);UnloadTexture(steel);
    if(font.texture.id && font.texture.id!=GetFontDefault().texture.id)UnloadFont(font);
    CloseWindow();return 0;
}
