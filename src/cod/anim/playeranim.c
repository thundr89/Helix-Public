/* GPL-2.0-or-later. CoD native animation-tree and player-script rules. */
#include "anim/playeranim.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NODES 2048
#define RULES 1024
#define ACTIONS 2048
#define DEFINES 128
#define CONDITIONS 10
#define TOKENS 65536

typedef struct Token {char text[128];int line;} Token;
typedef struct Tokens {Token* items;int count,at;char error[160];} Tokens;
typedef struct Node {char name[128];int parent,branch;char flags[96];} Node;
typedef struct Condition {char kind[32],values[384];} Condition;
typedef struct Rule {char context[64];Condition conditions[CONDITIONS];int nconditions,first,count;} Rule;
typedef struct Define {char kind[32],name[128],values[384];} Define;
struct CodAnimProgram {Node nodes[NODES];int nnodes;Rule rules[RULES];int nrules;CodAnimAction actions[ACTIONS];int nactions;Define defines[DEFINES];int ndefines;char clips[NODES][128];int nclips;};
static int same(const char* a,const char* b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
static int append(char* out,size_t cap,const char* text){size_t n=strlen(out),m=strlen(text);if(n+m+2>cap)return 0;if(n)out[n++]=' ';memcpy(out+n,text,m+1);return 1;}
static int tokenize(const char* source,Tokens* out){
    const char* p=source;int line=1;
    if(!source || strlen(source)>4*1024*1024)return 0;
    out->items=(Token*)calloc(TOKENS,sizeof(Token));if(!out->items)return 0;
    while(*p){Token* t;unsigned n=0;char quote=0;
        if(isspace((unsigned char)*p)){if(*p=='\n')line++;p++;continue;}
        if(p[0]=='/'&&p[1]=='/'){while(*p&&*p!='\n')p++;continue;}
        if(p[0]=='/'&&p[1]=='*'){p+=2;while(*p && !(p[0]=='*'&&p[1]=='/')){if(*p=='\n')line++;p++;}if(!*p)goto bad;p+=2;continue;}
        if(out->count>=TOKENS)goto bad;t=out->items+out->count++;t->line=line;
        if(strchr("{}:,=",*p)){t->text[0]=*p++;continue;}
        if(*p=='"'){quote=*p++;}
        while(*p && (quote?*p!=quote:!isspace((unsigned char)*p)&&!strchr("{}:,=",*p))){if(n+1>=sizeof(t->text))goto bad;t->text[n++]=*p++;}
        if(quote){if(*p!=quote)goto bad;p++;}if(!n)goto bad;
    }
    return 1;
bad:snprintf(out->error,sizeof(out->error),"invalid animation token at line %d",line);return 0;
}
static const char* peek(Tokens* t){return t->at<t->count?t->items[t->at].text:"";}
static int take(Tokens* t,const char* word){if(same(peek(t),word)){t->at++;return 1;}return 0;}
static int text(Tokens* t,char* dst,size_t cap){const char* p=peek(t);if(!*p||strlen(p)>=cap)return 0;strcpy(dst,p);t->at++;return 1;}
static int tree_nodes(CodAnimProgram* p,Tokens* t,int parent,int depth){
    if(depth>64)return 0;
    while(*peek(t) && strcmp(peek(t),"}")){
        Node* node;int index,i;
        if(p->nnodes>=NODES)return 0;index=p->nnodes++;node=p->nodes+index;node->parent=parent;
        if(!text(t,node->name,sizeof(node->name)) || strpbrk(node->name,"{}:,=\\/"))return 0;
        for(i=0;i<index;i++)if(same(node->name,p->nodes[i].name))return 0;
        if(take(t,":")){while(*peek(t)&&strcmp(peek(t),"{")){if(!append(node->flags,sizeof(node->flags),peek(t)))return 0;t->at++;}}
        if(take(t,"{")){node->branch=1;if(!tree_nodes(p,t,index,depth+1)||!take(t,"}"))return 0;}
    }
    return parent<0 || *peek(t);
}
static int supported(const char* key){const char* types[]={"weapons","weaponclass","movetype","weapon_position","mounted","firing","strafing","leaning","position","enemy_weapon","underwater","underhand"};unsigned i;for(i=0;i<sizeof(types)/sizeof(types[0]);i++)if(same(types[i],key))return 1;return 0;}
static int parse_rule(CodAnimProgram* p,Tokens* t,const char* context){
    Rule* rule;int line;
    if(p->nrules>=RULES)return 0;rule=p->rules+p->nrules++;snprintf(rule->context,sizeof(rule->context),"%s",context);rule->first=p->nactions;
    if(take(t,"default")){if(!take(t,"{"))return 0;}
    else {
        while(!take(t,"{")){
            Condition* c;if(rule->nconditions>=CONDITIONS || !*peek(t))return 0;c=rule->conditions+rule->nconditions++;
            if(!text(t,c->kind,sizeof(c->kind)) || !supported(c->kind))return 0;
            while(*peek(t)&&strcmp(peek(t),",")&&strcmp(peek(t),"{")){if(!append(c->values,sizeof(c->values),peek(t)))return 0;t->at++;}
            if(!*c->values)strcpy(c->values,"true");take(t,",");
        }
    }
    while(!take(t,"}")){
        CodAnimAction* a;if(!*peek(t)||p->nactions>=ACTIONS)return 0;
        a=p->actions+p->nactions++;rule->count++;line=t->items[t->at].line;
        while(*peek(t) && t->items[t->at].line==line && strcmp(peek(t),"}")){
            int layer=-1;
            if(take(t,"both"))layer=0;else if(take(t,"legs"))layer=1;else if(take(t,"torso"))layer=2;
            if(layer>=0){
                if(!text(t,a->clips[layer],sizeof(a->clips[layer])) || strspn(a->clips[layer],"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(a->clips[layer]))return 0;
                a->blend[layer]=100;
                while(*peek(t)&&t->items[t->at].line==line){
                    if(take(t,"duration") || take(t,"blendtime")){
                        const char* key=t->items[t->at-1].text;char* end;long n;if(!*peek(t))return 0;n=strtol(peek(t),&end,10);if(*end||n<0||n>60000)return 0;
                        if(same(key,"duration"))a->duration[layer]=(int)n;else a->blend[layer]=(int)n;t->at++;
                    }else if(take(t,"turretanim"))a->turret[layer]=1;
                    else break;
                }
            }else if(take(t,"sound")){if(!text(t,a->sound,sizeof(a->sound)))return 0;}
            else return 0;
        }
    }
    return 1;
}
static int player_script(CodAnimProgram* p,Tokens* t){
    char section[32]="",state[32]="";
    while(*peek(t)){
        if(take(t,"DEFINES")){strcpy(section,"defines");continue;}
        if(take(t,"ANIMATIONS")){strcpy(section,"animations");continue;}
        if(take(t,"EVENTS")){strcpy(section,"events");continue;}
        if(same(section,"defines")){
            Define* d;int line;
            if(!take(t,"set") || p->ndefines>=DEFINES)return 0;d=p->defines+p->ndefines++;line=t->items[t->at].line;
            if(!text(t,d->kind,sizeof(d->kind)) || !supported(d->kind) || !text(t,d->name,sizeof(d->name)) || !take(t,"="))return 0;
            while(*peek(t)&&t->items[t->at].line==line){if(!append(d->values,sizeof(d->values),peek(t)))return 0;t->at++;}continue;
        }
        if(same(section,"animations")){
            if(!take(t,"state") || !text(t,state,sizeof(state)) || !take(t,"{"))return 0;
            while(!take(t,"}")){
                char move[32],context[64];if(!text(t,move,sizeof(move)) || !take(t,"{"))return 0;
                snprintf(context,sizeof(context),"%s/%s",state,move);
                while(strcmp(peek(t),"}")){if(!*peek(t)||!parse_rule(p,t,context))return 0;}t->at++;
            }
        }else if(same(section,"events")){
            char context[64];if(!text(t,context,sizeof(context)) || !take(t,"{"))return 0;
            while(strcmp(peek(t),"}")){if(!*peek(t)||!parse_rule(p,t,context))return 0;}t->at++;
        }else return 0;
    }
    return 1;
}
CodAnimProgram* cod_anim_parse(const char* script,const char* tree,char* error,unsigned capacity){
    CodAnimProgram* p=(CodAnimProgram*)calloc(1,sizeof(*p));Tokens a={0},b={0};int i,j;
    if(!p)return NULL;
    if(!tokenize(tree,&a)||!tree_nodes(p,&a,-1,0)||a.at!=a.count || !tokenize(script,&b)||!player_script(p,&b))goto bad;
    for(i=0;i<p->nactions;i++)for(j=0;j<3;j++)if(*p->actions[i].clips[j]){
        int k;for(k=0;k<p->nclips;k++)if(same(p->clips[k],p->actions[i].clips[j]))break;
        if(k==p->nclips){if(k>=NODES)goto bad;strcpy(p->clips[p->nclips++],p->actions[i].clips[j]);}
    }
    free(a.items);free(b.items);return p;
bad:if(error&&capacity)snprintf(error,capacity,"animation parse rejected near tree token %d / script token %d (line %d)",a.at,b.at,b.at<b.count?b.items[b.at].line:0);
    free(a.items);free(b.items);free(p);return NULL;
}
void cod_anim_free(CodAnimProgram* p){free(p);}
int cod_anim_node(const CodAnimProgram* p,const char* name){int i;if(!p||!name)return 0;for(i=0;i<p->nnodes;i++)if(same(p->nodes[i].name,name))return p->nodes[i].branch?2:1;return 0;}
int cod_anim_clip_count(const CodAnimProgram* p){return p?p->nclips:0;}
const char* cod_anim_clip(const CodAnimProgram* p,int index){return p&&index>=0&&index<p->nclips?p->clips[index]:NULL;}
static int values_match(const CodAnimProgram* p,const char* kind,const char* expression,const char* actual,int depth){
    char buffer[384],*cursor,*word;int matched=0,subtract=0,negate=0;
    if(depth>16 || strlen(expression)>=sizeof(buffer))return 0;strcpy(buffer,expression);cursor=buffer;
    while(*cursor){int i,value=0;word=cursor;while(*cursor&&*cursor!=' ')cursor++;if(*cursor)*cursor++=0;while(*cursor==' ')cursor++;
        if(same(word,"AND"))continue;if(same(word,"MINUS")){subtract=1;continue;}if(same(word,"NOT")){negate=1;continue;}
        value=same(word,actual)||same(word,"all");
        for(i=0;i<p->ndefines;i++)if(same(p->defines[i].kind,kind)&&same(p->defines[i].name,word)){value=values_match(p,kind,p->defines[i].values,actual,depth+1);break;}
        if(subtract)matched=matched&&!value;else matched=matched||value;
    }
    return negate?!matched:matched;
}
static int matches(const CodAnimProgram* p,const Condition* c,const CodAnimInput* in){
    const hxSGAnimationInput* s=in->state;const char* actual=NULL;
    if(same(c->kind,"weapons"))actual=s->weapon_name;
    else if(same(c->kind,"weaponclass"))actual=s->weapon_class;
    else if(same(c->kind,"movetype"))actual=in->move;
    else if(same(c->kind,"weapon_position"))actual=s->ads?"ads":"hip";
    else if(same(c->kind,"mounted"))actual=in->mounted?"mg42":"none";
    else if(same(c->kind,"firing"))actual=s->firing?"true":"false";
    else if(same(c->kind,"strafing"))actual=in->strafing;
    else return 0; /* No fabricated state for an unavailable game condition. */
    return actual && values_match(p,c->kind,c->values,actual,0);
}
int cod_anim_select(const CodAnimProgram* p,const char* context,const CodAnimInput* input,unsigned choice,CodAnimAction* result){
    int i,j;if(!p||!context||!input||!input->state||!result)return 0;
    for(i=0;i<p->nrules;i++){const Rule* r=p->rules+i;if(!same(r->context,context))continue;
        for(j=0;j<r->nconditions;j++)if(!matches(p,r->conditions+j,input))break;
        if(j!=r->nconditions)continue;
        memset(result,0,sizeof(*result));if(r->count)*result=p->actions[r->first+choice%(unsigned)r->count];return 1;
    }
    return 0;
}
