#include "helix/engine.hpp"
#include <cstdio>
#include <cmath>
namespace hx {int run_unit_tests(){return 0;}}
int main(){hx::EngineConfig c;c.headless=true;c.basepath="cod_mg42_followup/runtime";c.homepath=c.basepath;c.game="test";c.exe_dir=".";hx::Engine e;if(!e.init(c))return 1;
int clock=10000,failures=0;
for(auto name:{"mp_carentan","mp_brecourt"}){e.load_map(name);hx::MapFile m;std::string err;hx::parse_map(e.fs().read_text(std::string("maps/")+name+".hxmap"),name,m,err);
for(auto& ent:m.entities){if(ent.get("classname")!="misc_mg42")continue;auto o=ent.get_vec3("origin");float yaw=ent.get_vec3("angles").y;auto f=hx::Angles{0,yaw,0}.to_forward();hx::Vec3 pos=o-f*46;auto floor=e.server().world().clip.box_trace(pos+hx::Vec3{0,0,100},pos-hx::Vec3{0,0,300},{-15,-15,-24},{15,15,32},17);pos=floor.endpos;
auto& s=e.server().slot(0);s.ps.origin=pos;s.ps.velocity={};s.ps.movement_flags=hx::PMF_ON_GROUND;s.ps.health=100;
auto eye=pos+s.ps.view_offset;auto los=e.server().world().clip.box_trace(eye,o,{},{},17);auto down=e.server().world().clip.box_trace(pos,pos-hx::Vec3{0,0,48},{-15,-15,-24},{15,15,32},17);auto stay=e.server().world().clip.box_trace(pos,pos,{-15,-15,-24},{15,15,32},17);
printf("GUN %s o=%g,%g,%g pos=%g,%g,%g ground=%g/%d LOS=%g/%d DOWN=%g/%d STAY=%d\n",name,o.x,o.y,o.z,pos.x,pos.y,pos.z,floor.fraction,floor.startsolid,los.fraction,los.startsolid,down.fraction,down.startsolid,stay.startsolid);
hx::UserCmd cmd;cmd.server_time=clock+=50;cmd.yaw=yaw;e.server().process_cmd(0,cmd);cmd.server_time=clock+=50;cmd.buttons=hx::BUTTON_USE;e.server().process_cmd(0,cmd);printf("USE=%d\n",(s.ps.movement_flags&hx::PMF_VIEW_LOCK)!=0);failures+=!(s.ps.movement_flags&hx::PMF_VIEW_LOCK);cmd.server_time=clock+=50;cmd.buttons=0;e.server().process_cmd(0,cmd);cmd.server_time=clock+=50;cmd.buttons=hx::BUTTON_USE;e.server().process_cmd(0,cmd);
}}
auto obj=e.fs().read_text("xmodel/mg42_bipod");hx::ModelAssets assets;int asset=assets.register_model("xmodel/mg42_bipod",true);assets.load_bytes(asset,obj.data(),obj.size());auto* slot=assets.slot(asset);int fixed=0,moving=0;for(auto& p:slot->parts){if(p.joint=="mg01")moving++;else fixed++;}printf("NATIVE PARTS fixed=%d moving=%d total=%zu\n",fixed,moving,slot->parts.size());failures+=fixed==0 || moving==0 || slot->parts.size()>16;
printf("TAGS %s\n",e.fs().read_text("xmodel/mg42_bipod.hxtags").c_str());e.shutdown();printf("Native MG42 probe: %d failures\n",failures);return failures?1:0;}
