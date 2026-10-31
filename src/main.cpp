#include "raylib.h"
#include "raymath.h"
#include "world.hpp"
#include "ai.hpp"
#include "agent.hpp"
#include "chat_text.hpp"
#include "chat_ui_layout.hpp"
#include "presentation_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
static Texture2D loadEnvironmentTexture(const char* filename,int seed,bool metal) {
    // The release bundle includes optional CC0 photographic materials.
    // A procedural fallback keeps development/offline builds runnable.
    if(FileExists(filename)){
        Texture2D texture=LoadTexture(filename);
        if(texture.id) {SetTextureFilter(texture,TEXTURE_FILTER_TRILINEAR);return texture;}
    }
    return makeTexture(seed,metal);
}
static void texturedBox(Texture2D texture,Vector3 pos,float sx,float sy,float sz,Color tint);
// A physically-aware lighting pass for all textured surfaces and native props.
// This is a compact forward renderer, not a PBR pipeline.
static Shader gPrisonLight{};
static Texture2D gWhite{};
static bool gUseLighting=false;
static const char *gLightVertex=R"SHADER(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 positionWS;
out vec3 normalWS;
out vec2 uv;
out vec4 colorVS;
void main(){
  positionWS=vec3(matModel*vec4(vertexPosition,1.0));
  normalWS=normalize(mat3(matModel)*vertexNormal);
  uv=vertexTexCoord;
  colorVS=vertexColor;
  gl_Position=mvp*vec4(vertexPosition,1.0);
}
)SHADER";
static const char *gLightFragment=R"SHADER(
#version 330
in vec3 positionWS;
in vec3 normalWS;
in vec2 uv;
in vec4 colorVS;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lampPos[6];
uniform vec3 cameraPos;
uniform vec3 flashForward;
uniform int powerOn;
uniform int flashOn;
void main(){
  vec4 albedo=texture(texture0,uv)*colDiffuse*colorVS;
  vec3 n=normalize(normalWS);
  vec3 light=vec3(powerOn==1 ? 0.67 : 0.24);
  for(int i=0;i<6;i++){
    vec3 v=lampPos[i]-positionWS;
    float distanceToLamp=length(v);
    float ndl=max(dot(n,normalize(v)),0.0);
    float falloff=1.0/(1.0+0.075*distanceToLamp+0.046*distanceToLamp*distanceToLamp);
    vec3 lampColor=(i%3==0)?vec3(1.0,0.91,0.78):vec3(0.79,0.9,1.0);
    light+=(powerOn==1?1.6:0.12)*ndl*falloff*lampColor;
  }
  if(flashOn==1){
    vec3 fromEye=positionWS-cameraPos;
    float len=length(fromEye);
    vec3 ray=normalize(fromEye+vec3(.00001));
    float focus=pow(max(dot(ray,normalize(flashForward)),0.0),21.0);
    light+=vec3(1.4,1.35,1.15)*focus/(1.0+0.12*len*len)*max(dot(n,normalize(-fromEye)),0.22);
  }
  vec3 lit=clamp(albedo.rgb*light,0.0,1.0);
  float haze=clamp((length(cameraPos-positionWS)-10.0)/45.0,0.0,0.32);
  vec3 rgb=mix(lit,vec3(0.065,0.09,0.105),haze);
  finalColor=vec4(pow(rgb,vec3(.95)),albedo.a);
}
)SHADER";
static void initLighting(){
    Image image=GenImageColor(2,2,WHITE);
    gWhite=LoadTextureFromImage(image);
    UnloadImage(image);
    gPrisonLight=LoadShaderFromMemory(gLightVertex,gLightFragment);
    gUseLighting=gPrisonLight.id!=0 && GetShaderLocation(gPrisonLight,"lampPos[0]")>=0;
}
static void setLighting(const World& world,Vector3 eye,Vector3 direction,bool flashlight){
    if(!gUseLighting)return;
    const float lamps[18]={
        5.0f,3.0f,4.0f, 5.0f,3.0f,13.0f,
        14.0f,3.0f,4.0f, 14.0f,3.0f,13.0f,
        22.0f,3.0f,5.0f, 22.0f,3.0f,15.0f
    };
    float pos[3]={eye.x,eye.y,eye.z}, forward[3]={direction.x,direction.y,direction.z};
    int power=world.lights?1:0, on=flashlight?1:0;
    SetShaderValueV(gPrisonLight,GetShaderLocation(gPrisonLight,"lampPos[0]"),lamps,SHADER_UNIFORM_VEC3,6);
    SetShaderValue(gPrisonLight,GetShaderLocation(gPrisonLight,"cameraPos"),pos,SHADER_UNIFORM_VEC3);
    SetShaderValue(gPrisonLight,GetShaderLocation(gPrisonLight,"flashForward"),forward,SHADER_UNIFORM_VEC3);
    SetShaderValue(gPrisonLight,GetShaderLocation(gPrisonLight,"powerOn"),&power,SHADER_UNIFORM_INT);
    SetShaderValue(gPrisonLight,GetShaderLocation(gPrisonLight,"flashOn"),&on,SHADER_UNIFORM_INT);
}
static void releaseLighting(){
    if(gUseLighting)UnloadShader(gPrisonLight);
    if(gWhite.id)UnloadTexture(gWhite);
}

static void box(float x,float y,float z,float sx,float sy,float sz,Color color){
    if(gWhite.id)texturedBox(gWhite,v3(x,y,z),sx,sy,sz,color);
    else DrawCube(v3(x,y,z),sx,sy,sz,color);
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
    if(gUseLighting)gSurfaceCube.materials[0].shader=gPrisonLight;
    DrawModelEx(gSurfaceCube,pos,v3(0,1,0),0,v3(sx,sy,sz),tint);
}
// Optional high-detail CC0 institutional meshes from the matching prison kit.
// All are drawn at authoring-scale (metres, Y up); load failure uses procedural props.
struct ImportedProps {
    Model bunk{},toilet{},camera{},desk{},fixture{},officer{};
};
static ImportedProps gProps{};
static Model loadProp(const char* filename) {
    if(!FileExists(filename))return {};
    Model model=LoadModel(filename);
    if(!model.meshCount)return {};
    // raylib replaces failed imports with a generic cube: reject it explicitly.
    int vertices=0;
    for(int i=0;i<model.meshCount;i++)vertices+=model.meshes[i].vertexCount;
    if(vertices<100){UnloadModel(model);return {};}
    return model;
}
static void initProps(){
    gProps.bunk=loadProp("assets/models/cell_bunk.glb");
    gProps.toilet=loadProp("assets/models/toilet_sink.glb");
    gProps.camera=loadProp("assets/models/cctv.glb");
    gProps.desk=loadProp("assets/models/control_desk.glb");
    gProps.fixture=loadProp("assets/models/ceiling_fixture.glb");
    gProps.officer=loadProp("assets/models/officer.glb");
}
static void freeProps(){
    for(Model *m:{&gProps.bunk,&gProps.toilet,&gProps.camera,&gProps.desk,&gProps.fixture,&gProps.officer})
        if(m->meshCount)UnloadModel(*m);
}
static bool drawProp(const Model& model,Vector3 p,float turn=0.f){
    if(model.meshCount==0)return false;
    DrawModelEx(model,p,v3(0,1,0),turn,v3(1,1,1),WHITE);
    return true;
}
static void drawDoor(const Door& d,Texture2D steel){
    DoorVisual leaf=doorVisual(d.x,d.z,d.open);
    const float x=d.x+.5f,z=d.z+.5f;
    // Heavy security doors fit the physical wall: the partition runs along Z.
    // Continuous reveals connect both faces of the meter-thick concrete wall.
    for(float jambZ:{z-.46f,z+.46f}) {
        box(x,1.36f,jambZ,1.02f,2.72f,.10f,rgba(43,54,62));
        box(x,1.36f,jambZ,1.06f,2.65f,.036f,rgba(90,96,98));
    }
    box(x,2.72f,z,1.04f,.15f,1.01f,rgba(43,54,62));
    box(x,.028f,z,1.04f,.052f,1.01f,rgba(68,72,71));
    // Rounded metallic surface treatments and viewport.
    texturedBox(steel,v3(leaf.centerX,1.32f,leaf.centerZ),leaf.depthX,2.55f,leaf.depthZ,rgba(203,212,214));
    if(!d.open){
        box(x-.065f,1.82f,z,.024f,.56f,.42f,rgba(17,25,29));
        box(x-.086f,1.82f,z,.018f,.43f,.33f,rgba(84,116,131));
        box(x+.065f,1.82f,z,.024f,.56f,.42f,rgba(17,25,29));
        box(x+.086f,1.82f,z,.018f,.43f,.33f,rgba(84,116,131));
        box(x-.09f,1.04f,z+.27f,.035f,.20f,.034f,rgba(180,190,191));
        box(x+.09f,1.04f,z+.27f,.035f,.20f,.034f,rgba(180,190,191));
        box(x-.094f,.46f,z,.01f,.06f,.60f,rgba(78,90,93));
    }else{
        box(leaf.centerX,1.36f,leaf.centerZ,.72f,.05f,.12f,rgba(61,75,77));
        beam(v3(x,0,z-.45f),v3(x,2.72f,z-.45f),.045f,rgba(156,164,164));
    }
    Color access=d.locked?rgba(243,81,65):rgba(90,220,170);
    // Both sides have functional visual access indicators.
    for(float side:{-1.f,1.f}){
        box(x+side*.55f,1.37f,z+.35f,.12f,.32f,.16f,rgba(20,31,36));
        box(x+side*.62f,1.45f,z+.35f,.025f,.057f,.065f,access);
    }
}
static void drawGuard(Point p,float angle,float time,bool alert,bool walking){
    if(gProps.officer.meshCount){
        float bob=walking?0.014f*std::sin(time*10.f):0.f;
        DrawModelEx(gProps.officer,v3(p.x,bob,p.z),v3(0,1,0),180.f-angle*RAD2DEG,v3(1,1,1),WHITE);
        if(alert)warningLight(p.x,2.12f,p.z,time,rgba(238,65,49));
        return;
    }
    // Articulated rounded anthropomorphic placeholder; character can later be replaced by a rigged asset.
    Vector3 base=v3(p.x,0,p.z);
    float sway=std::sin(time*6.f)*.085f;
    DrawCylinder(v3(p.x,.009f,p.z),.42f,.42f,.012f,20,rgba(0,0,0,92));
    Color pants=rgba(36,46,53),shirt=rgba(64,83,94),skin=rgba(185,147,121);
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
static void worldGeometry(const World& w,Texture2D concrete,Texture2D floor,Texture2D steel,float time) {
    // Deliberate material variation, surface detailing and repeated environmental storytelling.
    for(int z=1;z<World::height-1;z++)for(int x=1;x<World::width-1;x++){
        Color tint=((x+z)%4==0)?rgba(223,226,225):rgba(212,217,216);
        texturedBox(floor,v3(x+.5f,-.09f,z+.5f),1,.16f,1,tint);
        if((x+z*3)%5==0)box(x+.5f,-.002f,z+.5f,.02f,.003f,.72f,rgba(45,53,57,130));
    }
    for(int z=0;z<World::height;z++)for(int x=0;x<World::width;x++) {
        if(!w.solid(x,z))continue;
        const Door* d=w.doorAt(x,z);
        if(d)continue;
        bool exposed=!w.solid(x+1,z)||!w.solid(x-1,z)||!w.solid(x,z+1)||!w.solid(x,z-1);
        if(!exposed)continue;
        texturedBox(concrete,v3(x+.5f,1.57f,z+.5f),1,3.16f,1,rgba(224,225,221));
        box(x+.5f,.18f,z+.5f,1,.36f,1,rgba(38,47,52));
        box(x+.5f,3.04f,z+.5f,1,.24f,1,rgba(51,63,68));
        if((z+x*2)%3==0)box(x+.5f,1.6f,z+.5f,.012f,2.2f,.025f,rgba(39,46,51,95));
    }
    for(const auto& d:w.doors)drawDoor(d,steel);
    // Actual ceiling: previous builds rendered an empty black sky above the prison.
    for(int z=1;z<World::height-1;z++)for(int x=1;x<World::width-1;x++){
        if(w.solid(x,z))continue;
        texturedBox(concrete,v3(x+.5f,3.26f,z+.5f),1,.15f,1,rgba(212,219,217));
    }
    // Overhead maintenance conduits and a structural service tray.
    for(float zz:{4.5f,9.5f,14.5f}){
        box(14.5f,3.02f,zz,6.0f,.1f,.12f,rgba(52,69,79));
        box(5.5f,3.04f,zz,7.0f,.09f,.10f,rgba(75,85,89));
    }
    // Ceiling beams and lights: not merely a single uniform flat plane.
    for(int x=2;x<World::width-1;x+=4){
        box(x+.5f,3.21f,9.5f,.13f,.26f,16.3f,rgba(43,54,61));
        box(x+.5f,3.21f,16.4f,.14f,.23f,4.7f,rgba(43,54,61));
    }
    for(int z=3;z<World::height-1;z+=5){
        if(!drawProp(gProps.fixture,v3(5.2f,2.93f,z+.5f)))box(5.2f,3.04f,z+.5f,1.18f,.08f,.42f,rgba(196,205,192));
        if(!drawProp(gProps.fixture,v3(14.1f,2.93f,z+.5f)))box(14.1f,3.04f,z+.5f,1.18f,.08f,.42f,rgba(196,205,192));
        if(w.lights){
            box(5.2f,3.002f,z+.5f,1.07f,.01f,.32f,rgba(241,235,204));
            box(14.1f,3.002f,z+.5f,1.07f,.01f,.32f,rgba(241,235,204));
        }
    }
    for(int z=2;z<World::height-1;z+=4){
        beam(v3(9.75f,2.86f,z),v3(9.75f,2.86f,z+3.7f),.055f,rgba(84,95,97));
        beam(v3(18.24f,2.86f,z),v3(18.24f,2.86f,z+3.7f),.046f,rgba(82,90,99));
    }
    // Authored-scale detailed prison furniture replaces cube props when available.
    for(int z:{3,8,13}){
        if(!drawProp(gProps.bunk,v3(3.22f,0.f,z+.55f),90.f)){
            box(3.22f,.42f,z+.55f,2.f,.13f,.93f,rgba(57,66,71));
            box(3.22f,.515f,z+.55f,1.91f,.12f,.84f,rgba(129,138,138));
            box(2.68f,.59f,z+.55f,.65f,.1f,.7f,rgba(182,189,179));
            for(float x:{2.3f,4.14f})for(float zz:{z+.17f,z+.98f})
                beam(v3(x,.08f,zz),v3(x,.45f,zz),.045f,rgba(47,55,58));
        }
        if(!drawProp(gProps.toilet,v3(6.82f,0.f,z+.55f),90.f)){
            DrawCylinder(v3(6.82f,.35f,z+.55f),.21f,.25f,.7f,14,rgba(177,186,187));
            box(6.82f,.8f,z+.79f,.6f,.45f,.23f,rgba(143,158,160));
        }
        box(7.85f,.85f,z+.2f,.85f,1.7f,.72f,rgba(70,84,88));
        for(int n=0;n<3;n++)box(8.29f,.54f+n*.38f,z+.2f,.015f,.22f,.48f,rgba(39,49,55));
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
    // Modelled CCTV units on overhead mounts.
    for(Point cam:{Point{13.5f,3.5f},Point{22.5f,15.5f}}){
        if(!drawProp(gProps.camera,v3(cam.x,2.62f,cam.z),90.f)){
            beam(v3(cam.x,3.05f,cam.z),v3(cam.x,2.73f,cam.z),.055f,rgba(105,112,116));
            box(cam.x,2.65f,cam.z,.37f,.19f,.38f,rgba(54,66,73));
            DrawSphere(v3(cam.x,2.62f,cam.z+.215f),.11f,rgba(16,24,28));
        }
        if(w.cameras)warningLight(cam.x+.2f,2.78f,cam.z,time,rgba(239,51,42));
    }
    // Terminal and generator.
    if(!drawProp(gProps.desk,v3(21.5f,0.f,4.5f))){
        box(21.5f,.72f,4.5f,1.3f,1.44f,.67f,rgba(56,69,76));
        box(21.5f,1.21f,4.86f,.91f,.52f,.06f,w.cameras?rgba(17,120,119):rgba(87,45,40));
        for(int n=0;n<5;n++)box(21.2f+n*.14f,1.05f,4.902f,.085f,.015f,.008f,rgba(134,209,182));
    }
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
    Font f=LoadFontEx("C:/Windows/Fonts/segoeui.ttf",48,nullptr,0);
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
static void centeredText(Font font,const std::string& caption,float center,float y,float size,Color color){
    Vector2 bounds=MeasureTextEx(font,caption.c_str(),size,1.1f);
    text(font,caption,center-bounds.x*.5f,y,size,color);
}
static std::vector<std::string> wrapForFont(Font font,const std::string& message,float width,float size){
    return wrapChatText(message,std::max(12.f,width),[&](const std::string& part){
        return MeasureTextEx(font,part.c_str(),size,1.1f).x;
    });
}
static void drawIntercom(Font font,const AIService& ai,const std::string& input,int scroll){
    int sw=GetScreenWidth(),sh=GetScreenHeight();
    float scale=std::clamp(std::min(sw/1600.f,sh/900.f),.80f,1.40f);
    Color bone=rgba(235,240,237),dim=rgba(191,207,210),teal=rgba(142,225,207);

    // The ORIGINAL compact composer stays in its previous on-screen position.
    // Only history gains a translucent backing; the game remains visible.
    const float composerWidth=std::min(sw-50.f*scale,920.f*scale);
    const float pad=18.f*scale,bodySize=16.f*scale,lineHeight=23.f*scale;
    struct Entry {
        const char* role;
        Color color;
        std::vector<std::string> lines;
        float height=0;
    };
    std::vector<Entry> entries;
    float totalHeight=0.f;
    for(const auto& turn:ai.history()){
        for(int role=0;role<2;role++){
            const std::string& value=role==0?turn.player:turn.officer;
            Entry entry{role==0?"YOU":"VOSS",role==0?teal:bone,
                        wrapForFont(font,value,composerWidth-122.f*scale,bodySize),0.f};
            entry.height=std::max(1,(int)entry.lines.size())*lineHeight+10.f*scale;
            totalHeight+=entry.height;
            entries.push_back(std::move(entry));
        }
    }
    auto layout=compactChatLayout(sw,sh,scale,totalHeight+pad*2);
    if(!entries.empty()&&layout.historyHeight>0.f){
        // Floating, narrow translucent history panel (no full-screen dark overlay).
        Rectangle history={layout.historyX,layout.historyY,layout.historyWidth,layout.historyHeight};
        panel(history,rgba(6,18,25,layout.historyAlpha));
        float left=history.x+pad;
        float top=history.y+pad*.55f;
        float bottom=history.y+history.height-pad*.55f;
        float viewHeight=bottom-top;
        float maxScroll=std::max(0.f,totalHeight-viewHeight);
        float scrollOffset=std::clamp(scroll*37.f*scale,0.f,maxScroll);
        float y=bottom-totalHeight+scrollOffset;
        BeginScissorMode((int)(history.x+6*scale),(int)top,(int)(history.width-12*scale),
                         std::max(1,(int)viewHeight));
        for(const auto& entry:entries){
            if(y+entry.height>=top && y<=bottom){
                text(font,entry.role,left,y+2.f*scale,14.f*scale,entry.color);
                float bodyX=left+73.f*scale;
                for(size_t line=0;line<entry.lines.size();line++)
                    text(font,entry.lines[line],bodyX,y+(float)line*lineHeight,
                         bodySize,bone);
            }
            y+=entry.height;
        }
        EndScissorMode();
    }
    // Same original bottom-positioned composer, with wrapped input and no
    // injected T character. Its placement is not tied to history length.
    Rectangle dialog={layout.composerX,layout.composerY,
                      layout.composerWidth,layout.composerHeight};
    panel(dialog,rgba(4,15,20,241));
    DrawRectangle(dialog.x,dialog.y,3.f*scale,dialog.height,teal);
    text(font,"OFFICER VOSS  /  TWO-WAY PRISON INTERCOM",
         dialog.x+24.f*scale,dialog.y+18.f*scale,16.f*scale,teal);
    auto inputLines=wrapForFont(font,"> "+input+"_",dialog.width-47.f*scale,20.f*scale);
    const int count=std::min<int>((int)inputLines.size(),3);
    for(int line=0;line<count;line++)
        text(font,inputLines[inputLines.size()-count+line],dialog.x+24.f*scale,
             dialog.y+49.f*scale+line*24.f*scale,20.f*scale,bone);
    text(font,"ENTER  SEND     ESC  CANCEL",
         dialog.x+24.f*scale,dialog.y+128.f*scale,13.f*scale,dim);
}
static void overlay(const World& w,const AIService& ai,const AgentExecutor& executor,
                    Font font,bool chat,const std::string& input,bool pause,bool controls,bool debug,bool showMap,int chatScroll){
    int sw=GetScreenWidth(),sh=GetScreenHeight();
    float scale=std::clamp(std::min(sw/1600.f,sh/900.f),.80f,1.40f);
    float left=36*scale,top=26*scale;
    Color bone=rgba(232,233,223),dim=rgba(175,192,195),teal=rgba(151,225,210),red=rgba(244,103,92);
    // Transparent architectural HUD: information is overlaid, never embedded in web-like panels.
    DrawRectangleGradientV(0,0,sw,static_cast<int>(113*scale),rgba(6,13,19,180),rgba(6,13,19,0));
    text(font,"BLACKWATER",left,top,24*scale,bone);
    text(font,"BLOCK C  /  MAXIMUM SECURITY",left,top+32*scale,13*scale,dim);
    const std::string alarm="SECURITY "+std::to_string(w.alert)+" / 3";
    const float right=sw-230*scale;
    text(font,alarm,right,top,17*scale,w.alert>1?red:teal);
    text(font,w.lockdown?"LOCKDOWN IN EFFECT":"PATROL NORMAL",right,top+29*scale,12*scale,dim);

    // In-world reticle, subtler than a web cursor.
    float cx=sw*.5f,cy=sh*.5f;
    DrawCircleLines((int)cx,(int)cy,4*scale,rgba(232,242,240,175));
    DrawCircle((int)cx,(int)cy,1.5f*scale,rgba(249,249,237));
    std::string hint=w.interactionHint();
    if(!pause&&!chat&&!w.caught&&!w.escaped&&!hint.empty()){
        float width=MeasureTextEx(font,hint.c_str(),19*scale,1.1f).x+52*scale;
        Rectangle prompt={cx-width/2,cy+32*scale,width,42*scale};
        panel(prompt,rgba(9,24,30,212));
        centeredText(font,hint,cx,prompt.y+9*scale,19*scale,bone);
    }
    DrawRectangleGradientV(0,sh-(int)(200*scale),sw,(int)(200*scale),rgba(5,12,17,0),rgba(4,10,16,213));
    std::string task=!w.hasHairpin?"FIND A TOOL TO PICK THE CELL LOCK":
                      !w.hasCard?"REACH SECURITY  /  RECOVER A KEYCARD":
                      "REACH THE EXIT  /  ESCAPE BLACKWATER";
    text(font,"OBJECTIVE",left,sh-133*scale,13*scale,teal);
    text(font,task,left,sh-102*scale,18*scale,bone);
    const float by=sh-50*scale;
    text(font,"STAMINA",left,by-4*scale,12*scale,dim);
    bar(left+85*scale,by+5*scale,150*scale,w.player.stamina,teal);
    text(font,w.player.crouching?"CROUCHED":w.player.moving?"MOVING":"STILL",left+256*scale,by-5*scale,12*scale,dim);
    text(font,w.hasHairpin?"HAIRPIN": "NO TOOL",left+370*scale,by-4*scale,12*scale,w.hasHairpin?teal:dim);
    text(font,w.hasCard?"KEYCARD":"NO CARD",left+470*scale,by-4*scale,12*scale,w.hasCard?teal:dim);

    if(w.elapsed<18.f&&!chat&&!pause) {
        centeredText(font,"WASD  MOVE     E  INTERACT     T  SPEAK     F1  CONTROLS",cx,sh-32*scale,12*scale,dim);
    }

    // Full-width wrapping prevents replies from being cut at 110 characters.
    // The transcript remains accessible through T, even on a smaller monitor.
    if(chat) {
        drawIntercom(font,ai,input,chatScroll);
    } else if((ai.hasPendingConversation() || !w.guardSpeech.empty() ||
               w.status.rfind("VOSS RADIO ERROR:",0)==0)&&!pause) {
        float maxWidth=std::min(sw-48.f*scale,1130.f*scale);
        float px=(sw-maxWidth)*.5f;
        bool error=w.status.rfind("VOSS RADIO ERROR:",0)==0;
        const std::string fullMessage=ai.hasPendingConversation()?
            "OFFICER VOSS IS PROCESSING YOUR MESSAGE...":
            error?w.status:"VOSS:  "+w.guardSpeech;
        float fontSize=17.f*scale,lineHeight=25.f*scale;
        auto lines=wrapForFont(font,fullMessage,maxWidth-45.f*scale,fontSize);
        float height=22.f*scale+(float)lines.size()*lineHeight;
        float bottom=sh-177.f*scale;
        Rectangle subtitle={px,bottom-height,maxWidth,height};
        panel(subtitle,rgba(5,17,23,241));
        DrawRectangle((int)(px+3.f*scale),(int)(subtitle.y+7.f*scale),
                      std::max(2,(int)(3*scale)),(int)(height-14.f*scale),error?red:teal);
        for(size_t line=0;line<lines.size();line++)
            text(font,lines[line],px+20.f*scale,subtitle.y+10.f*scale+(float)line*lineHeight,
                 fontSize,error?red:bone);
    }

    if(showMap&&!pause&&!chat)minimap(w,sw,sh);
    if(debug&&!pause){
        panel({sw-380*scale,112*scale,344*scale,98*scale},rgba(9,19,26,213));
        text(font,ai.status.substr(0,34),sw-363*scale,125*scale,15*scale,teal);
        text(font,executor.description().substr(0,32),sw-363*scale,153*scale,13*scale,dim);
        text(font,"FPS "+std::to_string(GetFPS()),sw-363*scale,179*scale,12*scale,dim);
    }
    if(controls&&!pause&&!chat){
        panel({sw*.5f-350*scale,sh*.5f-170*scale,700*scale,340*scale},rgba(5,18,23,244));
        centeredText(font,"CONTROLS",cx,sh*.5f-134*scale,33*scale,bone);
        const char* lines[]={
            "WASD  /  MOVE             MOUSE  /  LOOK",
            "LEFT SHIFT  /  SPRINT     C  /  CROUCH",
            "E  /  INTERACT           T  /  VOSS INTERCOM",
            "F  /  FLASHLIGHT         TAB  /  MAP",
            "ESC  /  PAUSE             F11  /  FULLSCREEN",
            "F1  /  CLOSE HELP         F12  /  SCREENSHOT"
        };
        for(int i=0;i<6;i++)centeredText(font,lines[i],cx,sh*.5f-74*scale+i*36*scale,16*scale,dim);
    }
    if(pause) {
        DrawRectangle(0,0,sw,sh,rgba(3,8,12,206));
        centeredText(font,"BLACKWATER",cx,sh*.5f-160*scale,51*scale,bone);
        centeredText(font,"PAUSED",cx,sh*.5f-77*scale,22*scale,teal);
        DrawLineEx({cx-185*scale,sh*.5f-22*scale},{cx+185*scale,sh*.5f-22*scale},1.5f*scale,rgba(76,114,120));
        centeredText(font,"ESC  RESUME GAME",cx,sh*.5f+14*scale,21*scale,bone);
        centeredText(font,"R  RESTART   F11  FULLSCREEN   Q  EXIT",cx,sh*.5f+62*scale,16*scale,dim);
    }
    if(w.caught||w.escaped) {
        DrawRectangle(0,0,sw,sh,rgba(3,7,10,218));
        centeredText(font,w.escaped?"YOU MADE IT OUT":"YOU WERE DETAINED",cx,sh*.5f-80*scale,47*scale,w.escaped?teal:red);
        centeredText(font,w.escaped?"BLACKWATER HAS LOST CONTROL":"OFFICER VOSS STOPPED YOUR ESCAPE",cx,sh*.5f+9*scale,17*scale,dim);
        centeredText(font,"R  NEW ATTEMPT",cx,sh*.5f+71*scale,22*scale,bone);
    }
}
int main(){
    // Native Windows maximize controls are available in this resizable window.
    SetConfigFlags(FLAG_MSAA_4X_HINT|FLAG_VSYNC_HINT|FLAG_WINDOW_RESIZABLE);
    InitWindow(1280,720,"AI PRISON BREAK  |  BLACKWATER");
    SetWindowMinSize(900,540);
    MaximizeWindow();
    SetExitKey(KEY_NULL);
    SetTargetFPS(std::clamp(GetMonitorRefreshRate(GetCurrentMonitor()),60,240));
    DisableCursor();
    Font font=interfaceFont();
    initLighting();
    initProps();
    Texture2D concrete=loadEnvironmentTexture("assets/textures/concrete_wall_001.jpg",53,false);
    Texture2D floor=loadEnvironmentTexture("assets/textures/concrete_floor_worn_001.jpg",46,false);
    Texture2D steel=loadEnvironmentTexture("assets/textures/rusty_metal_02.jpg",23,true);
    World w;
    AIService ai;
    AgentExecutor executor;
    float yaw=1.77f,pitch=0.f,worldTick=0.f,brainTick=0.f;
    bool paused=false,chat=false,flashlight=true,controls=false,debug=false,quit=false;
    bool borderlessFullscreen=false;
    bool restoreMaximized=true;
    std::string input;
    int chatScroll=0;
    const bool previewCapture=std::getenv("AI_PRISON_PREVIEW")!=nullptr;
    int captureFrames=0;
    while(!WindowShouldClose()&&!quit){
        float dt=std::min(.05f,GetFrameTime());
        int sw=GetScreenWidth(),sh=GetScreenHeight();
        // F11 and Alt+Enter: preserve the decorated window's maximize state.
        const bool toggleDisplay=IsKeyPressed(KEY_F11) ||
            ((IsKeyDown(KEY_LEFT_ALT)||IsKeyDown(KEY_RIGHT_ALT))&&IsKeyPressed(KEY_ENTER));
        if(toggleDisplay) {
            if(!borderlessFullscreen) {
                restoreMaximized=IsWindowMaximized();
                if(restoreMaximized)RestoreWindow();
                ToggleBorderlessWindowed();
                borderlessFullscreen=IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
            } else {
                ToggleBorderlessWindowed();
                borderlessFullscreen=IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
                if(!borderlessFullscreen && restoreMaximized)MaximizeWindow();
            }
            if(!paused&&!chat&&!controls)DisableCursor();
        }
        if(IsKeyPressed(KEY_F1))controls=!controls;
        if(IsKeyPressed(KEY_F3))debug=!debug;
        if(paused&&IsKeyPressed(KEY_Q))quit=true;
        if(IsKeyPressed(KEY_ESCAPE)){
            if(chat){chat=false;chatScroll=0;input.clear();DisableCursor();}
            else {paused=!paused;if(paused)EnableCursor();else DisableCursor();}
        }
        if(IsKeyPressed(KEY_R)&&(paused||w.caught||w.escaped)){
            w=World{};executor.clear();yaw=1.77f;pitch=0;worldTick=0;brainTick=0;
            paused=false;chat=false;chatScroll=0;input.clear();DisableCursor();
        }
        if(!paused&&!controls&&!w.caught&&!w.escaped){
            bool openedChatThisFrame=false;
            if(IsKeyPressed(KEY_T)&&!chat){
                chat=true;input.clear();chatScroll=0;
                openedChatThisFrame=true;EnableCursor();
            }
            if(chat){
                // Raylib queues T both as a hotkey and as a typed character.
                // Drain and ignore opening-frame characters to prevent "> t_".
                std::vector<int> typed;
                for(int c=GetCharPressed();c!=0;c=GetCharPressed())typed.push_back(c);
                appendChatInput(input,typed,openedChatThisFrame,180);
                const float scrollWheel=GetMouseWheelMove();
                if(scrollWheel>0.f)chatScroll=std::min(1000,chatScroll+(int)std::ceil(scrollWheel)*2);
                else if(scrollWheel<0.f)chatScroll=std::max(0,chatScroll+(int)std::floor(scrollWheel)*2);
                if(IsKeyPressed(KEY_PAGE_UP))chatScroll=std::min(1000,chatScroll+6);
                if(IsKeyPressed(KEY_PAGE_DOWN))chatScroll=std::max(0,chatScroll-6);
                if(IsKeyPressed(KEY_BACKSPACE)&&!input.empty())input.pop_back();
                if(IsKeyPressed(KEY_ENTER)){
                    if(!input.empty()&&ai.submitMessage(w,input)){w.log("Prisoner: "+input,w.player.pos,1);w.status="MESSAGE SENT VIA INTERCOM - WAITING FOR VOSS";input.clear();chat=false;DisableCursor();}
                    else w.status="MESSAGE EMPTY OR QUEUE FULL";
                }
            }else{
                Vector2 mouse=GetMouseDelta();yaw=applyMouseYaw(yaw,mouse.x);pitch=std::clamp(pitch-mouse.y*.00225f,-1.34f,1.34f);
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
            executor.tick(w,dt);
            brainTick+=dt;
            if(brainTick>8.0f&&!ai.busy()&&!ai.hasPendingConversation()&&!chat&&!executor.active()){ai.requestPatrol(w);brainTick=0;}
        }
        if(auto reply=ai.poll()){
            if(reply->kind==ReplyKind::Conversation) {
                if(!reply->error.empty()) {
                    w.status="VOSS RADIO ERROR: "+reply->error.substr(0,135);
                } else {
                    if(!reply->speech.empty())w.command("speak",reply->speech);
                    w.status="VOSS REPLIED ON INTERCOM";
                    executor.setPlan(reply->actions);
                }
            } else if(reply->error.empty()) {
                // Patrol planning is SILENT. No unsolicited, hallucinated dialogue.
                executor.setPlan(reply->actions);
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
        setLighting(w,camera.position,dir,flashlight);
        worldGeometry(w,concrete,floor,steel,w.elapsed);
        drawGuard(w.guard.pos,w.guard.angle,w.elapsed,w.alert>1,w.guard.moving);
        if(flashlight){
            // Player-controlled flashlight glow / near-cone cue. Dynamic lighting material pass is future work.
            Vector3 impact=Vector3Add(camera.position,Vector3Scale(dir,3.3f));
            DrawSphere(impact,.075f,rgba(205,219,178,30));
        }
        EndMode3D();
        overlay(w,ai,executor,font,chat,input,paused,controls,debug,IsKeyDown(KEY_TAB),chatScroll);
        EndDrawing();
        if(IsKeyPressed(KEY_F12)&&!previewCapture)TakeScreenshot("AIPrisonBreak-preview.png");
        if(previewCapture && ++captureFrames>=30){
            TakeScreenshot("AIPrisonBreak-preview.png");
            break;
        }
    }
    UnloadTexture(concrete);UnloadTexture(floor);UnloadTexture(steel);
    freeProps();
    if(gSurfaceCube.meshCount)UnloadModel(gSurfaceCube);
    releaseLighting();
    if(font.texture.id && font.texture.id!=GetFontDefault().texture.id)UnloadFont(font);
    CloseWindow();return 0;
}
