/* GPL-2.0-or-later. Diagnostic runner for unmodified retail GSC through hxfs_cod4.
 * This is a developer probe, not a replacement CoD4 gametype or shipping sgame. */
#include "helix/fs/iasset_plugin.h"
#include "gsc/compile.h"
#include "gsc/linked.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern hxAssetPlugin* helix_asset_plugin_create(void);
typedef struct Probe {
    hxAssetPlugin* assets;
    int reads,missing,errors,count;
    struct {char name[128],value[512];} cvars[128];
} Probe;
static void error(void* user,const char* text){
    Probe* p=(Probe*)user;if(p->errors++<30)fprintf(stderr,"%s\n",text);
}
static int read_script(void* user,const char* path,char** text,size_t* size){
    Probe* p=(Probe*)user;unsigned char* raw=NULL;unsigned n=0;char* copy;
    *text=NULL;*size=0;++p->reads;
    if(!p->assets->read(p->assets,path,&raw,&n)){++p->missing;fprintf(stderr,"missing asset: %s\n",path);return 0;}
    if(n>1024u*1024u){p->assets->free_buf(p->assets,raw);return 0;}
    copy=(char*)malloc((size_t)n+1);if(!copy){p->assets->free_buf(p->assets,raw);return 0;}
    memcpy(copy,raw,n);copy[n]=0;p->assets->free_buf(p->assets,raw);*text=copy;*size=n;return 1;
}
static const char* get_cvar(void* user,const char* name){
    Probe* p=(Probe*)user;int i;for(i=0;i<p->count;i++)if(!strcmp(p->cvars[i].name,name))return p->cvars[i].value;return "";
}
static void set_cvar(void* user,const char* name,const char* value){
    Probe* p=(Probe*)user;int i;
    if(strlen(name)>=sizeof(p->cvars[0].name) || strlen(value)>=sizeof(p->cvars[0].value)){error(user,"probe cvar exceeds capacity");return;}
    for(i=0;i<p->count;i++)if(!strcmp(p->cvars[i].name,name))break;
    if(i==128){error(user,"probe cvar limit");return;}
    if(i==p->count)++p->count;
    strcpy(p->cvars[i].name,name);strcpy(p->cvars[i].value,value);
}
int main(int argc,char** argv){
    Probe data={0};GscProg program;GscCompileDiagnostic diagnostic;
    GscHost host={0};GscRuntime* runtime=NULL;char* source=NULL;size_t size=0;int ok=0,i;
    if(argc<3 || argc>5 || (argc==4 && strcmp(argv[3],"--link")) || (argc==5 && strcmp(argv[3],"--run"))){
        fprintf(stderr,"usage: cod4_gsc_probe <retail-root> <script.gsc> [--link | --run entry]\n");return 2;
    }
    data.assets=helix_asset_plugin_create();if(!data.assets)return 2;
    if(!data.assets->open_dir(data.assets,argv[1]) || !read_script(&data,argv[2],&source,&size))goto done;
    if(!gsc_compile_runtime_options(source,size,&program,NULL,&diagnostic)){
        fprintf(stderr,"%s:%u:%u: %s\n",argv[2],diagnostic.line,diagnostic.column,diagnostic.message);goto done;
    }
    printf("compiled unchanged asset: %s (%zu bytes, %d functions, %d instructions)\n",argv[2],size,program.nfunc,program.ncode);gsc_prog_free(&program);
    if(argc==3){ok=1;goto done;}
    host.user=&data;host.read_file=read_script;host.get_cvar=get_cvar;host.set_cvar=set_cvar;host.error=error;
    if(argc==4){
        runtime=gsc_runtime_create_linked(&host,NULL,0);
        if(runtime && gsc_runtime_add_source(runtime,argv[2],source,size))ok=gsc_runtime_link(runtime);
        printf("closed-link preparation (no game-native bindings registered): %s\n",ok?"ok":"FAILED");
    }else{
        /* Existing compatibility VM: unknown executed natives fail; no native-call stub.
         * Includes/dependencies are real assets too. No filtered or rewritten source. */
        runtime=gsc_runtime_create(&host);
        if(runtime && gsc_runtime_start(runtime,argv[2],source,size,argv[4],-1) && !gsc_runtime_errors(runtime)){
            for(i=0;i<100 && gsc_runtime_threads(runtime) && !gsc_runtime_errors(runtime);i++)gsc_runtime_tick(runtime,i*50,100000);
            ok=!gsc_runtime_errors(runtime) && !gsc_runtime_threads(runtime);
        }
        for(i=0;i<data.count;i++)printf("script cvar: %s=%s\n",data.cvars[i].name,data.cvars[i].value);
        printf("compatibility VM entry %s: %s\n",argv[4],ok?"completed":"FAILED");
    }
    if(runtime)printf("asset reads=%d missing=%d runtime errors=%d\n",data.reads,data.missing,gsc_runtime_errors(runtime));
    if(data.errors || data.missing)ok=0;
done:
    gsc_runtime_destroy(runtime);free(source);data.assets->destroy(data.assets);return ok?0:1;
}
