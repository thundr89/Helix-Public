#include "helix/engine.hpp"
#include "helix/render/backend.hpp"
#include <cstdio>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace hx {int run_unit_tests(){return 0;}}
int main(int argc,char** argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc<2)return 1;int failures=0;
auto check=[&](bool ok,const char* message){printf("CHECK %s %s\n",message,ok?"PASS":"FAIL");failures+=!ok;};
hx::NativeWindow window;check(window.create("Skybox pixel regression",128,128),"window");auto gpu=hx::create_named_backend(argv[1]);if(!gpu || !gpu->init(window.native_window(),window.native_display(),128,128))return 1;gpu->set_vsync(false);
hx::clear_map_albedos();const unsigned char colors[6][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{255,0,255,255},{0,255,255,255}};
std::array<hx::Material,6> faces;for(int i=0;i<6;i++){std::vector<hx::u8> pixels(16*16*4);for(int k=0;k<256;k++)memcpy(pixels.data()+k*4,colors[i],4);faces[i].name="environment_face";faces[i].skybox=true;faces[i].cull=hx::CullMode::None;faces[i].honor_cull=true;faces[i].two_sided=true;faces[i].atlas_tile=hx::register_map_albedo(std::to_string(i).c_str(),pixels.data(),16,16,0);}
gpu->reload_albedos();

hx::RenderWorld world;world.set_gpu(gpu.get());world.set_backend(hx::gpu_backend_kind(argv[1]));world.set_skybox(faces);hx::View view;view.width=view.height=128;view.reverse_z=true;
const hx::Angles angles[]={{0,0,0},{0,180,0},{0,90,0},{0,270,0},{-89,0,0},{89,0,0}};
std::vector<hx::u8> pixels;int w,h;
auto read=[&](){if(gpu->read_rgba(pixels,w,h))return true;
    gpu->wait_idle();window.pump();Sleep(100);w=window.width();h=window.height();POINT origin={0,0};ClientToScreen((HWND)window.hwnd(),&origin);
    HDC screen=GetDC(NULL),copy=CreateCompatibleDC(screen);BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* data=NULL;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&data,NULL,0);if(!bitmap){DeleteDC(copy);ReleaseDC(NULL,screen);return false;}auto old=SelectObject(copy,bitmap);BOOL ok=BitBlt(copy,0,0,w,h,screen,origin.x,origin.y,SRCCOPY|CAPTUREBLT);pixels.resize(w*h*4);auto* src=(unsigned char*)data;for(int i=0;i<w*h;i++){pixels[i*4]=src[i*4+2];pixels[i*4+1]=src[i*4+1];pixels[i*4+2]=src[i*4];pixels[i*4+3]=255;}SelectObject(copy,old);DeleteObject(bitmap);DeleteDC(copy);ReleaseDC(NULL,screen);return !!ok;};

for(int i=0;i<6;i++){view.angles=angles[i];world.render(view);check(read(),"pixel readback");if(pixels.empty())return 1;auto* c=pixels.data()+((h/2)*w+w/2)*4;printf("FACE %d rgb=%u,%u,%u\n",i,c[0],c[1],c[2]);bool match=true;for(int k=0;k<3;k++)match&=std::abs(int(c[k])-colors[i][k])<12;check(match,"face orientation and unlit color");}
view.angles={0,0,0};auto wall=hx::make_box_mesh({90,-20,-20},{110,20,20});hx::Material foreground;foreground.name="wall";foreground.albedo={0,1,0};foreground.two_sided=true;foreground.cull=hx::CullMode::None;world.add_mesh(&wall,&foreground,hx::Mat4::identity(),0);world.render(view);read();auto* c=pixels.data()+((h/2)*w+w/2)*4;check(c[0]<100 && c[1]>c[0],"foreground occludes skybox");
hx::EngineConfig cfg;cfg.headless=true;cfg.basepath="cod_skybox/runtime";cfg.homepath=cfg.basepath;cfg.exe_dir=".";cfg.game="test";hx::Engine engine;check(engine.init(cfg),"native plugin init");auto& render=engine.render();render.set_gpu(gpu.get());render.set_backend(hx::gpu_backend_kind(argv[1]));
int hashes[4]={};const char* expected_sky[]={"env/mpcarentan_rt","env/hedgerow_rt","env/pavlovtest_rt","env/stormyfogged_rt"};int index=0;
for(auto name:{"mp_carentan","mp_brecourt","mp_harbor","mp_hurtgen"}){check(engine.load_map(name),"native map loads");check(render.has_skybox(),"native six-face skybox loaded");printf("NATIVE %s %s\n",name,engine.cvars().find("r_mapSkybox")->string_value.c_str());check(engine.cvars().find("r_mapSkybox")->string_value==expected_sky[index],"correct retail sky selected");gpu->reload_albedos();render.clear_frame();view.angles={-35,0,0};render.render(view);read();hx::write_png_rgba((std::string("cod_skybox/")+argv[1]+"-"+name+".png").c_str(),pixels.data(),w,h);for(size_t k=size_t(w*h*3);k<pixels.size();k++)hashes[index]=int((unsigned(hashes[index])*31+pixels[k])&0x7fffffff);index++;}
check(hashes[0]!=hashes[1],"retail maps have different sky pixels");
const char* invalid="map { version 1 name missing_sky }\nbrush { mins -128 -128 -32 maxs 128 128 0 }\nentity { \"classname\" \"environment_skybox\" \"runtime_only\" \"1\" \"face_rt\" \"env/mpcarentan_rt\" }\nentity { \"classname\" \"info_player_deathmatch\" \"origin\" \"0 0 24\" }\n";
engine.fs().write_home_file("maps/missing_sky.hxmap",invalid,strlen(invalid));check(engine.load_map("missing_sky"),"missing-face map loads safely");check(!render.has_skybox() && engine.cvars().find("r_mapSkybox")->string_value.empty(),"incomplete skybox falls back without prior map sky");
render.set_gpu(nullptr);engine.shutdown();check(!render.has_skybox(),"map unload releases skybox");gpu->shutdown();window.destroy();printf("Skybox pixel test: %d failures\n",failures);return failures?1:0;}
