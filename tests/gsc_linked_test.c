/* GPL-2.0-or-later. Closed-link runtime acceptance slice; no retail scripts. */
#include "gsc/linked.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures,calls,fallbacks,reads;
static char last_error[512];
#define CHECK(c) do {++checks;if(!(c)){++failures;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);}}while(0)
static void error(void* user,const char* text){(void)user;snprintf(last_error,sizeof(last_error),"%s",text);}
static int fallback(void* u,const char* name,int method,int receiver,const GscNativeValue* args,int argc,GscNativeValue* out){
    (void)u;(void)name;(void)method;(void)receiver;(void)args;(void)argc;(void)out;++fallbacks;return 1;
}
static int native(void* user,int method,int receiver,const GscNativeValue* args,int argc,GscNativeValue* out){
    CHECK(user==&calls);CHECK(argc==1 && args[0].kind==GSC_NATIVE_INT);
    CHECK(!method || receiver==42);++calls;out->kind=GSC_NATIVE_INT;out->integer=args[0].integer+1;return 1;
}
static int reject(void* u,int method,int receiver,const GscNativeValue* args,int argc,GscNativeValue* out){
    (void)u;(void)method;(void)receiver;(void)args;(void)argc;(void)out;return -1;
}
static int read_file(void* user,const char* path,char** text,size_t* size){
    const char* source="helper(x){return x+10;}";(void)user;++reads;
    if(strcmp(path,"maps/library.gsc"))return 0;
    *size=strlen(source);*text=(char*)malloc(*size+1);if(!*text)return 0;memcpy(*text,source,*size+1);return 1;
}
static GscRuntime* create(void){
    GscHost host={0};GscNativeBinding binding={"probe",GSC_BIND_FUNCTION|GSC_BIND_METHOD,native,&calls};
    host.error=error;host.native_call=fallback;host.read_file=read_file;
    return gsc_runtime_create_linked(&host,&binding,1);
}
static void rejects_before_execution(void){
    const char* bad[]={
        "main(){level.started=1;} unused(){missing(1);}",
        "main(){self missing(1);}",
        "main(){level.fn=::missing;}",
        "main(){maps\\absent::missing();}",
        "main(){thread missing();}",
        "main(){level.value=1; @;}"
    };size_t i;
    for(i=0;i<sizeof(bad)/sizeof(*bad);++i){
        GscRuntime* r=create();int value=0;CHECK(r);last_error[0]=0;
        if(gsc_runtime_add_source(r,"maps/bad.gsc",bad[i],strlen(bad[i])))CHECK(!gsc_runtime_link(r));
        CHECK(strstr(last_error,"maps/bad.gsc:")!=NULL);
        CHECK(!gsc_runtime_start(r,"maps/bad.gsc",NULL,0,"main",42));
        CHECK(!gsc_runtime_level_int(r,"started",&value));CHECK(gsc_runtime_threads(r)==0);CHECK(last_error[0]);gsc_runtime_destroy(r);
    }
}
static void dynamic_gameplay(void){
    const char* source=
        "#include maps\\library;\n"
        "main(){level.inventory=[];level.inventory[\"mod_key\"]=5;"
        "level.callback=::calculate;level.answer=[[level.callback]](level.inventory[\"mod_key\"]);"
        "level.external=helper(2);level.native=probe(level.answer);self probe(9);level.clock=getTime();level.magnitude=abs(-3);"
        "level thread listener();wait .05;level notify(\"custom_event\",77);}\n"
        "calculate(x){return x*2;}\n"
        "listener(){level endon(\"stop\");level waittill(\"custom_event\",value);level.received=value;wait .05;level.done=1;}";
    GscRuntime* r=create();int value=0,before;CHECK(r);
    CHECK(gsc_runtime_add_source(r,"maps/main.gsc",source,strlen(source)));
    CHECK(calls==0);CHECK(gsc_runtime_threads(r)==0);CHECK(gsc_runtime_link(r));CHECK(gsc_runtime_link(r));before=reads;
    CHECK(!gsc_runtime_add_source(r,"maps/late.gsc",source,strlen(source)));
    CHECK(gsc_runtime_start(r,"MAPS\\MAIN.GSC",NULL,0,"main",42));
    gsc_runtime_tick(r,0,20000);CHECK(gsc_runtime_level_int(r,"answer",&value) && value==10);
    CHECK(gsc_runtime_level_int(r,"external",&value) && value==12);
    CHECK(gsc_runtime_level_int(r,"native",&value) && value==11);CHECK(calls==2 && fallbacks==0);
    CHECK(gsc_runtime_level_int(r,"clock",&value) && value==0);
    CHECK(!gsc_runtime_level_int(r,"received",&value));gsc_runtime_tick(r,50,20000);gsc_runtime_tick(r,51,20000);
    CHECK(gsc_runtime_level_int(r,"received",&value) && value==77);
    gsc_runtime_tick(r,101,20000);CHECK(gsc_runtime_level_int(r,"done",&value) && value==1);
    CHECK(reads==before);CHECK(gsc_runtime_errors(r)==0);gsc_runtime_destroy(r);
}
static void rejection_and_event_ownership(void){
    GscHost host={0};GscNativeBinding binding={"reject",GSC_BIND_FUNCTION,reject,NULL};
    const char* source="main(){reject();level.after=1;}";int value=0;GscRuntime* r;
    host.error=error;r=gsc_runtime_create_linked(&host,&binding,1);CHECK(r);
    CHECK(gsc_runtime_add_source(r,"reject.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(gsc_runtime_start(r,"reject.gsc",NULL,0,"main",42));gsc_runtime_tick(r,0,20000);
    CHECK(gsc_runtime_errors(r)==1);CHECK(!gsc_runtime_level_int(r,"after",&value));gsc_runtime_destroy(r);
    source="main(){self endon(\"disconnect\");self waittill(\"custom\",v);level.payload=v;}";
    r=create();CHECK(gsc_runtime_add_source(r,"events.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(gsc_runtime_start(r,"events.gsc",NULL,0,"main",42));gsc_runtime_tick(r,0,20000);
    CHECK(gsc_runtime_is_waiting(r,42,"custom"));gsc_runtime_notify(r,-1,"custom",NULL,0);gsc_runtime_tick(r,1,20000);
    CHECK(gsc_runtime_is_waiting(r,42,"custom"));gsc_runtime_notify(r,42,"disconnect",NULL,0);gsc_runtime_tick(r,2,20000);
    CHECK(gsc_runtime_threads(r)==0);CHECK(!gsc_runtime_level_int(r,"payload",&value));gsc_runtime_destroy(r);
}
static void registry_ownership(void){
    GscHost host={0};char name[]="probe";GscNativeBinding bindings[2]={{name,GSC_BIND_FUNCTION,native,&calls},{"PROBE",GSC_BIND_FUNCTION,native,&calls}};
    const char* source="main(){level.value=probe(2);}";GscRuntime* r;
    CHECK(!gsc_runtime_create_linked(&host,bindings,2));CHECK(!gsc_runtime_create_linked(&host,NULL,1));
    r=gsc_runtime_create_linked(&host,bindings,1);CHECK(r);name[0]='x';bindings[0].call=NULL;
    CHECK(gsc_runtime_add_source(r,"main.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(gsc_runtime_start(r,"main.gsc",NULL,0,"main",-1));gsc_runtime_tick(r,0,20000);CHECK(gsc_runtime_errors(r)==0);gsc_runtime_destroy(r);
}
static void required_roots(void){
    const char* source="utility(){return 1;}";GscRuntime* r=create();
    CHECK(gsc_runtime_add_source(r,"utility.gsc",source,strlen(source)));
    CHECK(gsc_runtime_require_entry(r,"utility.gsc","utility"));
    CHECK(!gsc_runtime_require_entry(r,"utility.gsc","main"));
    CHECK(!gsc_runtime_link(r));CHECK(!gsc_runtime_start(r,"utility.gsc",NULL,0,"utility",42));
    CHECK(gsc_runtime_threads(r)==0);gsc_runtime_destroy(r);
}
static const char* mode_cvar(void* u,const char* name){(void)u;return !strcmp(name,"g_gametype")?"hq":"";}
static int empty_find(void* u,const char* key,const char* value,int* ids,int capacity){(void)u;(void)key;(void)value;(void)ids;(void)capacity;return 0;}
static const char* model_key(void* u,int id,const char* key){(void)u;return id==7 && !strcmp(key,"classname")?"script_model":"";}
static int spawn_fixture(void* u,int receiver,int entity,const GscNativeValue* args,int argc,GscNativeValue* out){
    (void)u;(void)receiver;(void)entity;(void)args;(void)argc;out->kind=GSC_NATIVE_ENTITY;out->integer=7;return 1;
}
static void dynamic_entity_lookup(void){
    const char* source="main(){level.equal=(getCvar(\"g_gametype\")==\"hq\");if(level.equal){e=spawn();e.targetname=\"hqradio\";e.hasobj[2]=e;e.hasobj[4]=17;}level.count=getEntArray(\"hqradio\",\"targetname\").size;}";
    GscHost host={0};GscNativeBinding binding={"spawn",GSC_BIND_FUNCTION,spawn_fixture,NULL};GscRuntime* r;int value=0;
    host.get_cvar=mode_cvar;host.entity_find=empty_find;host.entity_key=model_key;host.error=error;
    r=gsc_runtime_create_linked(&host,&binding,1);CHECK(r);CHECK(gsc_runtime_add_source(r,"lookup.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(gsc_runtime_start(r,"lookup.gsc",NULL,0,"main",-1));gsc_runtime_tick(r,0,20000);
    CHECK(gsc_runtime_level_int(r,"equal",&value) && value==1);CHECK(gsc_runtime_level_int(r,"count",&value) && value==1);
    {int ids[2]={-1,-1};GscArgument out={0};
        CHECK(gsc_runtime_find_entities(r,"targetname","hqradio",ids,2)==1 && ids[0]==7 && ids[1]==-1);
        CHECK(gsc_runtime_find_entities(r,"targetname","missing",ids,2)==0);
        CHECK(gsc_runtime_find_entities(r,"targetname","hqradio",NULL,0)==0);
        CHECK(gsc_runtime_get_entity_index(r,7,"hasobj",2,&out) && out.kind==4 && out.integer==7);
        CHECK(gsc_runtime_get_entity_index(r,7,"hasobj",4,&out) && out.kind==1 && out.integer==17);
        CHECK(!gsc_runtime_get_entity_index(r,7,"hasobj",1,&out) && out.kind==0);
        CHECK(!gsc_runtime_get_entity_index(r,7,"hasobj",8,&out));
        CHECK(!gsc_runtime_get_entity_index(r,7,"targetname",0,&out));
        CHECK(!gsc_runtime_get_entity_index(r,7,"hasobj",-1,&out));
    }
    CHECK(gsc_runtime_errors(r)==0);gsc_runtime_destroy(r);
}
static void delayed_shared_state(void){
    const char* source="main(){level.ticks=0;thread observe();wait .05;level.entries=[];level.entries[0]=7;wait .05;level.ready=1;}observe(){for(;;){level.ticks++;if(isDefined(level.entries)){for(i=0;i<level.entries.size;i++){if(level.entries[i]==7){level.seen=1;break;}}}wait .05;}}";
    GscRuntime* r=create();int value=0;CHECK(gsc_runtime_add_source(r,"delayed.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(gsc_runtime_start(r,"delayed.gsc",NULL,0,"main",-1));
    for(int time=0;time<=500;time+=50)gsc_runtime_tick(r,time,20000);
    CHECK(gsc_runtime_level_int(r,"ready",&value) && value==1);CHECK(gsc_runtime_level_int(r,"seen",&value) && value==1);CHECK(gsc_runtime_errors(r)==0);gsc_runtime_destroy(r);
}
static int cleanup_calls;
static int destroy_fixture(void* u,int method,int entity,const GscNativeValue* args,int argc,GscNativeValue* out){
    (void)u;(void)args;(void)out;CHECK(method && entity==7 && argc==0);++cleanup_calls;return 1;
}
static int cleanup_dispatch(void* u,const char* name,int method,int entity,const GscNativeValue* args,int argc,GscNativeValue* out){
    if(!strcmp(name,"spawn"))return spawn_fixture(u,method,entity,args,argc,out);
    if(!strcmp(name,"destroy"))return destroy_fixture(u,method,entity,args,argc,out);
    return 0;
}
static void retired_cleanup(void){
    const char* source="main(){old=spawn();old destroy();replacement=spawn();old destroy();level.alive=isDefined(replacement);replacement destroy();level.done=1;}";
    GscHost host={0};GscNativeBinding bindings[]={{"spawn",GSC_BIND_FUNCTION,spawn_fixture,NULL},{"destroy",GSC_BIND_METHOD,destroy_fixture,NULL}};
    GscRuntime* r;int value=0;host.error=error;cleanup_calls=0;
    r=gsc_runtime_create_linked(&host,bindings,2);CHECK(r);CHECK(gsc_runtime_add_source(r,"cleanup.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(gsc_runtime_start(r,"cleanup.gsc",NULL,0,"main",-1));gsc_runtime_tick(r,0,20000);
    CHECK(cleanup_calls==2);CHECK(gsc_runtime_level_int(r,"alive",&value) && value==1);
    CHECK(gsc_runtime_level_int(r,"done",&value) && value==1);CHECK(gsc_runtime_errors(r)==0);gsc_runtime_destroy(r);
    host.native_call=cleanup_dispatch;cleanup_calls=0;r=gsc_runtime_create(&host);CHECK(r);
    CHECK(gsc_runtime_start(r,"cleanup.gsc",source,strlen(source),"main",-1));gsc_runtime_tick(r,0,20000);
    CHECK(cleanup_calls==2);CHECK(gsc_runtime_level_int(r,"alive",&value) && value==1);
    CHECK(gsc_runtime_level_int(r,"done",&value) && value==1);CHECK(gsc_runtime_errors(r)==0);gsc_runtime_destroy(r);
}
typedef struct DvarFixture {char value[64];int writes,reads;} DvarFixture;
static const char* dvar_read(void* user,const char* name){
    DvarFixture* d=(DvarFixture*)user;++d->reads;return strcmp(name,"runtime_value")?"":d->value;
}
static void dvar_write(void* user,const char* name,const char* value){
    DvarFixture* d=(DvarFixture*)user;CHECK(!strcmp(name,"runtime_value"));
    snprintf(d->value,sizeof(d->value),"%s",value);++d->writes;
}
static void dvar_aliases(void){
    const char* source="main(){setDvar(\"runtime_value\",\"12.5\");"
        "level.integer=getDvarInt(\"runtime_value\");"
        "level.floating=(getDvarFloat(\"runtime_value\")==12.5);"
        "level.text=(getDvar(\"runtime_value\")==\"12.5\");"
        "level.absent=(getDvar(\"absent\")==\"\" && getDvarInt(\"absent\")==0 && getDvarFloat(\"absent\")==0);"
        "setDvar(\"runtime_value\",9);level.legacy=getCvarInt(\"runtime_value\");}";
    DvarFixture data={{0},0,0};GscHost host={0};GscRuntime* r;int value=0;
    host.user=&data;host.get_cvar=dvar_read;host.set_cvar=dvar_write;host.error=error;
    r=gsc_runtime_create_linked(&host,NULL,0);CHECK(r);if(!r)return;
    CHECK(gsc_runtime_add_source(r,"dvars.gsc",source,strlen(source)));CHECK(gsc_runtime_link(r));
    CHECK(data.writes==0 && !data.value[0]); // Preparation may query developer, but executes no script.
    data.reads=0;
    CHECK(gsc_runtime_start(r,"dvars.gsc",NULL,0,"main",-1));gsc_runtime_tick(r,0,20000);
    CHECK(gsc_runtime_errors(r)==0 && data.writes==2 && data.reads==7);
    CHECK(gsc_runtime_level_int(r,"integer",&value) && value==12);
    CHECK(gsc_runtime_level_int(r,"floating",&value) && value==1);
    CHECK(gsc_runtime_level_int(r,"text",&value) && value==1);
    CHECK(gsc_runtime_level_int(r,"absent",&value) && value==1);
    CHECK(gsc_runtime_level_int(r,"legacy",&value) && value==9);
    gsc_runtime_destroy(r);
}
int main(void){rejects_before_execution();dynamic_gameplay();registry_ownership();rejection_and_event_ownership();required_roots();dynamic_entity_lookup();delayed_shared_state();retired_cleanup();dvar_aliases();printf("linked GSC: %d checks, %d failures\n",checks,failures);return failures?1:0;}
