/* GPL-2.0-or-later. Deterministic malformed-input and compiler scaling tests. */
#include "gsc/compile.h"
#include "gsc/vm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL: %s (line %d)\n", m, __LINE__);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static uint32_t random_state = 0x8ca6123u;
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void check_input(const char *source, size_t size) {
    GscProg p;
    GscCompileDiagnostic error;
    int ok = gsc_compile_runtime_options(source, size, &p, NULL, &error), i;
    if (ok) {
        for (i = 0; i < p.ncode; i++)
            CHECK(p.code[i].op != GSC_DIAGNOSTIC,
                  "strict compiler never emits skipped-statement recovery");
    } else {
        CHECK(!p.code && !p.funcs && !p.strs && !p.stroff && !p.strhash && !p.ncode && !p.nfunc,
              "failure is atomic");
        CHECK(error.message[0] != 0 && error.line > 0 && error.column > 0,
              "failure has a source diagnostic");
    }
    gsc_prog_free(&p);
}
static void native_error(void* u,const char* s){(void)u;fprintf(stderr,"NATIVE: %s\n",s);}
static int native_calls,animation_references;
static void fixture_animation_reference(void* user,const char* tree,const char* clip){(void)user;CHECK(tree&&!strcmp(tree,"multiplayer"),"animation declarations retain their tree during module loading");if(clip)CHECK(!strcmp(clip,"walk"),"animation leaf declaration retains clip name");animation_references++;}
static int fixture_native(void* user,const char* name,int receiver,int entity,
                          const GscNativeValue* args,int argc,GscNativeValue* out) {
    (void)user;
    if(!strcmp(name,"animprobe")) {
        CHECK(!receiver && argc==2 && args[0].kind==GSC_NATIVE_ANIMATION &&
              args[1].kind==GSC_NATIVE_ANIMTREE && !strcmp(args[0].tree,"multiplayer") &&
              !strcmp(args[0].text,"walk") && !strcmp(args[1].tree,"multiplayer"),
              "native animation argument keeps tree and clip identity");
        *out=args[0];native_calls++;return 1;
    }
    if(!strcmp(name,"nativeprobe")) {
        CHECK(!receiver&&argc==3&&args[0].kind==GSC_NATIVE_STRING&&
              !strcmp(args[0].text,"payload")&&args[1].kind==GSC_NATIVE_VECTOR&&
              args[1].vector[2]==3&&args[2].kind==GSC_NATIVE_INT&&args[2].integer==7,
              "native bridge preserves argument types and order");
        native_calls++;out->kind=GSC_NATIVE_ENTITY;out->integer=42;return 1;
    }
    if(!strcmp(name,"readprobe")) {
        CHECK(receiver&&entity==42&&argc==0,"native method receives stable entity identity");
        out->kind=GSC_NATIVE_INT;out->integer=91;native_calls++;return 1;
    }
    return 0;
}
static int rejected_native_field(void* user,int entity,const char* name,const GscNativeValue* value,GscNativeValue* result){
    (void)user;(void)result;
    return entity==42&&value&&!strcmp(name,"rejected")?-1:0;
}
static void test_native(void) {
    const char* source="main(){level.clock=gettime();level.len=int(length((3,4,0)));"
        "level.dist=int(distance((1,2,3),(1,2,8)));v=vectornormalize((0,0,4));"
        "level.norm=int(v[2]);level.randomok=1;for(i=0;i<1000;i++){n=randomint(7);f=randomfloat(2);if(n<0||n>=7||f<0||f>=2)level.randomok=0;}e=nativeprobe(\"payload\",(1,2,3),7);level.result=e readprobe();}";
    GscHost host;GscRuntime* r;int n;
    memset(&host,0,sizeof(host));host.native_call=fixture_native;host.error=native_error;
    r=gsc_runtime_create(&host);
    CHECK(r&&gsc_runtime_start(r,"native.gsc",source,strlen(source),"main",-1),"native fixture compiles");
    if(!r)return;
    gsc_runtime_tick(r,1234,200000);
    CHECK(!gsc_runtime_errors(r)&&native_calls==2,"native calls actually dispatch");
    CHECK(gsc_runtime_level_int(r,"clock",&n)&&n==1234,"native time uses simulation clock");
    CHECK(gsc_runtime_level_int(r,"len",&n)&&n==5,"native vector length result");
    CHECK(gsc_runtime_level_int(r,"dist",&n)&&n==5,"native distance result");
    CHECK(gsc_runtime_level_int(r,"norm",&n)&&n==1,"native normalization result");
    CHECK(gsc_runtime_level_int(r,"randomok",&n)&&n==1,"native random results stay inside exclusive bounds");
    CHECK(gsc_runtime_level_int(r,"result",&n)&&n==91,"native method returns value to script");
    gsc_runtime_destroy(r);
    host.native_field=rejected_native_field;
    source="main(){e=nativeprobe(\"payload\",(1,2,3),7);e.rejected=1;level.unreachable=1;}";
    r=gsc_runtime_create(&host);
    CHECK(r&&gsc_runtime_start(r,"field.gsc",source,strlen(source),"main",-1),"native field fixture compiles");
    if(r){gsc_runtime_tick(r,0,20000);CHECK(gsc_runtime_errors(r)==1&&!gsc_runtime_level_int(r,"unreachable",&n),"rejected native field stops its thread without VM shadow assignment");gsc_runtime_destroy(r);}
    source="main(){missing_native();level.unreachable=1;}";
    r=gsc_runtime_create(&host);
    CHECK(r&&gsc_runtime_start(r,"missing.gsc",source,strlen(source),"main",-1),"unbound fixture compiles");
    if(r){gsc_runtime_tick(r,0,20000);CHECK(gsc_runtime_errors(r)==1&&
        !gsc_runtime_level_int(r,"unreachable",&n),"unbound native stops instead of fake success");gsc_runtime_destroy(r);}
}

static void test_animation_bridge(void){
    GscHost host={0};GscRuntime* r;int value=0;
    const char* source="#using_animtree(\"multiplayer\"); main(){a=%walk;t=#animtree;"
        "b=animprobe(a,t);wait .05;level.animationok=(a==b);level.treesame=(t==#animtree);}";
    host.native_call=fixture_native;host.error=native_error;host.animation_reference=fixture_animation_reference;native_calls=0;animation_references=0;
    r=gsc_runtime_create(&host);
    CHECK(r && gsc_runtime_start(r,"anim.gsc",source,strlen(source),"main",-1),"animation bridge fixture compiles");
    if(r){gsc_runtime_tick(r,0,20000);gsc_runtime_tick(r,50,20000);
        CHECK(!gsc_runtime_errors(r) && native_calls==1,"animation values dispatch without coercion or fake native success");
        CHECK(animation_references>=2,"animation references are announced at module loading before script execution");
        CHECK(gsc_runtime_level_int(r,"animationok",&value) && value==1,"native animation return survives yielding with tree identity");
        CHECK(gsc_runtime_level_int(r,"treesame",&value) && value==1,"animtree retains its distinct type");
        gsc_runtime_destroy(r);}
}
int main(void) {
    test_native();test_animation_bridge();
    {
        GscProg p;GscCompileDiagnostic d;
        const char* empty="setAnim(animName,val,val,val){ /* unused compatibility arguments */ }";
        CHECK(gsc_compile_runtime_options(empty,strlen(empty),&p,NULL,&d),"unchanged empty IW3 helper compiles");
        if(p.nfunc)CHECK(p.funcs[0].nparams==4 && p.funcs[0].nlocals==4,"all positional arguments retain slots");
        gsc_prog_free(&p);
        empty="setAnim(animName,val,val,val){return val;}";
        CHECK(!gsc_compile_runtime_options(empty,strlen(empty),&p,NULL,&d) && strstr(d.message,"duplicate parameter"),"ambiguous named arguments in executable bodies remain rejected");
        gsc_prog_free(&p);
    }
    static const char source[] = "#include common; "
                                 "main(){a=[];for(i=0;i<4;i++){a[i]=(i+1)*2;}if(a[1]>=4){level."
                                 "value=a[1];}else{level.value=0;}} helper(x){return (int)x%3;}";
    static const char alphabet[] = "{}[]();,+-*/%&|^~!<>=#\\\"0123abc \n";
    size_t i, n = strlen(source);
    char mutated[sizeof(source) + 4];
    for (i = 0; i <= n; i++)
        check_input(source, i);
    for (i = 0; i < 4096; i++) {
        size_t at = (size_t)next_random() % (n + 1), size = n;
        memcpy(mutated, source, n + 1);
        switch (next_random() % 3) {
        case 0:
            if (at < n)
                mutated[at] = alphabet[next_random() % (sizeof(alphabet) - 1)];
            break;
        case 1:
            if (at < n) {
                memmove(mutated + at, mutated + at + 1, n - at);
                size--;
            }
            break;
        default:
            memmove(mutated + at + 1, mutated + at, n - at + 1);
            mutated[at] = alphabet[next_random() % (sizeof(alphabet) - 1)];
            size++;
            break;
        }
        check_input(mutated, size);
    }
    {
        GscProg p;
        GscCompileDiagnostic diagnostic;
        CHECK(!gsc_compile_runtime_options(NULL, 0, &p, NULL, &diagnostic),
              "NULL source rejects cleanly");
        gsc_prog_free(&p);
    }
    {
        char *large = (char *)malloc(65536);
        size_t size = 0;
        GscProg p;
        GscCompileDiagnostic diagnostic;
        GscRuntime *runtime;
        int value = -1;
        CHECK(large != NULL, "scaling fixture allocation");
        if (large) {
            for (i = 0; i < 1000; i++)
                size += (size_t)sprintf(large + size, "function%u(){return %u;}\n", (unsigned)i,
                                        (unsigned)i);
            size += (size_t)sprintf(large + size, "main(){level.value=function999();}");
            CHECK(gsc_compile_runtime_options(large, size, &p, NULL, &diagnostic) &&
                      p.nfunc == 1001,
                  "all 1001 functions are compiled");
            gsc_prog_free(&p);
            runtime = gsc_runtime_create(NULL);
            CHECK(runtime != NULL, "runtime creation");
            if (runtime) {
                CHECK(gsc_runtime_start(runtime, "scaling.gsc", large, size, "main", -1),
                      "large program links");
                gsc_runtime_tick(runtime, 0, 200000);
                CHECK(gsc_runtime_level_int(runtime, "value", &value) && value == 999 &&
                          !gsc_runtime_errors(runtime),
                      "last function executes with correct identity");
                gsc_runtime_destroy(runtime);
            }
            free(large);
        }
    }
    {
        const char *deep = "main(){level.count=0;thread chain(0);level.done=1;}"
                           "chain(n){level.count++;if(n<200)thread chain(n+1);}";
        GscRuntime *runtime = gsc_runtime_create(NULL);
        int count = -1, done = -1;
        CHECK(runtime && gsc_runtime_start(runtime, "threads.gsc", deep, strlen(deep), "main", -1),
              "deep inline thread program links");
        if (runtime) {
            gsc_runtime_tick(runtime, 0, 200000);
            CHECK(gsc_runtime_level_int(runtime, "count", &count) && count == 201 &&
                      gsc_runtime_level_int(runtime, "done", &done) && done == 1 &&
                      !gsc_runtime_errors(runtime),
                  "201 nested inline threads preserve order without native C stack recursion");
            gsc_runtime_destroy(runtime);
        }
    }
    {
        char *source = (char *)malloc(8192);
        size_t size = 0;
        GscRuntime *runtime;
        int value = -1;
        CHECK(source != NULL, "argument fixture allocation");
        if (source) {
            size += (size_t)sprintf(source + size, "last(");
            for (i = 0; i < 128; i++)
                size += (size_t)sprintf(source + size, "%sa%u", i ? "," : "", (unsigned)i);
            size += (size_t)sprintf(source + size, "){return a127;}main(){level.value=last(");
            for (i = 0; i < 128; i++)
                size += (size_t)sprintf(source + size, "%s%u", i ? "," : "", (unsigned)i);
            size += (size_t)sprintf(source + size, ");}");
            runtime = gsc_runtime_create(NULL);
            CHECK(runtime && gsc_runtime_start(runtime, "arguments.gsc", source, size, "main", -1),
                  "128-argument call links");
            if (runtime) {
                gsc_runtime_tick(runtime, 0, 200000);
                CHECK(gsc_runtime_level_int(runtime, "value", &value) && value == 127 &&
                          !gsc_runtime_errors(runtime),
                      "argument values are not truncated at former 16-argument limit");
                gsc_runtime_destroy(runtime);
            }
            free(source);
        }
    }
    {
        const char* source="main(){level.callback=::bye;}bye(){wait .05;level.retiredToken=self.pers[\"token\"];self.pers[\"token\"]=\"finished\";}";
        GscRuntime* r=gsc_runtime_create(NULL);GscArgument value={0},actual={0};int retired;
        CHECK(gsc_runtime_start(r,"retire.gsc",source,strlen(source),"main",-1),"retired client program links");gsc_runtime_tick(r,0,10000);
        value.kind=3;value.text="old connection";gsc_runtime_set_entity_value(r,-3,"pers","token",&value);
        CHECK(gsc_runtime_call_level(r,"callback",-3,NULL,0),"disconnect coroutine starts");gsc_runtime_tick(r,0,10000);
        retired=gsc_runtime_retire_entity(r,-3);CHECK(retired< -1000 && gsc_runtime_entity_busy(r,retired),"yielded disconnect retains a detached object");
        value.text="new connection";gsc_runtime_set_entity_value(r,-3,"pers","token",&value);gsc_runtime_tick(r,100,10000);
        CHECK(gsc_runtime_get_entity_value(r,-1,"retiredToken",NULL,&actual) && !strcmp(actual.text,"old connection"),"disconnect resumes with old pers");
        CHECK(gsc_runtime_get_entity_value(r,-3,"pers","token",&actual) && !strcmp(actual.text,"new connection"),"disconnect cannot mutate reused client slot");
        CHECK(!gsc_runtime_entity_busy(r,retired) && !gsc_runtime_errors(r),"detached coroutine completes without cancellation");gsc_runtime_destroy(r);
    }
    {
        const char* source="main(){game.token=123;level.ephemeral=9;level.bye=::bye;level.after=::after;}bye(){wait .05;level.oldNumber=self getEntityNumber();}after(){level.result=game.token;level.persToken=self.pers[\"token\"];level.ephemeralGone=!isdefined(level.ephemeral);}";
        GscRuntime* r=gsc_runtime_create(NULL);GscArgument value={0},out={0};int number=-1;
        CHECK(gsc_runtime_start(r,"preserve.gsc",source,strlen(source),"main",-1),"preserving transition program links");gsc_runtime_tick(r,0,10000);
        value.kind=3;value.text="persistent";gsc_runtime_set_entity_value(r,-3,"pers","token",&value);
        gsc_runtime_call_level(r,"bye",-3,NULL,0);gsc_runtime_tick(r,0,10000);gsc_runtime_retire_entity(r,-3);gsc_runtime_tick(r,100,10000);
        CHECK(gsc_runtime_level_int(r,"oldNumber",&number) && number==0,"yielded disconnect keeps original client number");
        value.text="next player";gsc_runtime_set_entity_value(r,-3,"pers","token",&value);
        CHECK(gsc_runtime_reset_map(r),"preserving transition clears map threads");
        CHECK(gsc_runtime_start(r,"preserve.gsc",source,strlen(source),"after",-3),"new map enters retained compiled function");gsc_runtime_tick(r,0,10000);
        CHECK(gsc_runtime_level_int(r,"result",&number) && number==123,"preserving transition retains game object");
        CHECK(gsc_runtime_get_entity_value(r,-1,"persToken",NULL,&out) && out.kind==3 && !strcmp(out.text,"next player"),"preserving transition retains player pers");
        CHECK(gsc_runtime_level_int(r,"ephemeralGone",&number) && number==1 && !gsc_runtime_errors(r),"preserving transition discards level fields without VM errors");gsc_runtime_destroy(r);
    }
    if (failures) {
        fprintf(stderr, "%d frontend checks failed\n", failures);
        return 1;
    }
    printf("GSC strict frontend: malformed-prefix/mutation corpus and 1001-function execution "
           "passed\n");
    return 0;
}
