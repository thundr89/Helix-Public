/*
 * Assemble an xmodel OBJ from archive entries.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "xmodel/helix.h"
#include "archive/archive.h"
#include "xmodel/read.h"
#include "cod_util.h"
#include "emit.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char g_player_body[160];
static const unsigned char* g_motion_anim;
static unsigned g_motion_size,g_motion_frame;

/* Idle poses the fingers. Hip fire is frame 0 of ADS_up: CoD holds that clip
 * at time 0 when the player is not aiming, and the clip only moves tag_torso.
 * The aimed pose is the last frame of the same clip. */
static void pose_weapon(const CodArchive* ar,const char* idle,const char* up) {
    unsigned char* anim = NULL;
    unsigned asz = 0;
    cod_xmodel_set_anim_pose(NULL, 0);
    if (!ar) return;
    if (idle && cod_archive_read(ar, idle, &anim, &asz))
        cod_xmodel_set_anim_pose(anim, asz);
    free(anim);
    anim = NULL;
    asz = 0;
    if (up && cod_archive_read(ar, up, &anim, &asz))
        cod_xmodel_overlay_anim_pose(anim, asz);
    free(anim);
}

static void pose_mp44_hands(const CodArchive* ar){pose_weapon(ar,"xanim/viewmodel_mp44_idle","xanim/viewmodel_mp44_ADS_up");}

void cod_set_player_body(const char* xmodel) {
    g_player_body[0] = '\0';
    if (!xmodel || !xmodel[0]) return;
    snprintf(g_player_body, sizeof(g_player_body), "%s", xmodel);
}

/* CoD viewmodels point along +X. Helix view space is +Z forward, +Y up. */
static int rewrite_view_basis(unsigned char** buf, unsigned int* size) {
    const char* p;
    const char* end;
    char* out;
    size_t cap, n = 0;
    if (!buf || !*buf || !size) return 0;
    cap = (size_t)(*size) + 64;
    out = (char*)malloc(cap);
    if (!out) return 0;
    p = (const char*)*buf;
    end = p + *size;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        char line[256];
        int vn = 0;
        float x, y, z;
        const char* src = p;
        size_t slen = len;
        if (len < sizeof(line)) {
            memcpy(line, p, len);
            line[len] = '\0';
            if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') vn = 1;
            if ((vn || (line[0] == 'v' && line[1] == ' ')) &&
                sscanf(line + (vn ? 3 : 2), "%f %f %f", &x, &y, &z) == 3) {
                /* Same P as cod_view_basis_rigid: CoD +Y becomes -right. */
                snprintf(line, sizeof(line), vn ? "vn %.6g %.6g %.6g" : "v %.6g %.6g %.6g", -y, z, x);
                src = line;
                slen = strlen(line);
            }
        }
        if (n + slen + 2 > cap) {
            size_t ncap = cap * 2 + slen + 2;
            char* grown = (char*)realloc(out, ncap);
            if (!grown) {
                free(out);
                return 0;
            }
            out = grown;
            cap = ncap;
        }
        memcpy(out + n, src, slen);
        n += slen;
        if (nl) out[n++] = '\n';
        p += len;
        if (nl) p++;
    }
    out[n] = '\0';
    free(*buf);
    *buf = (unsigned char*)out;
    *size = (unsigned int)n;
    return 1;
}

const char* cod_gameplay_xmodel(const char* name) {
    if (!name || !name[0]) return NULL;
    if (cod_strcasecmp(name, "models/player.obj") == 0) {
        if (g_player_body[0]) return g_player_body;
        return "xmodel/playerbody_american_airborne";
    }
    /* The host still draws two slots. These names are the ones it asks for. */
    if (cod_strcasecmp(name, "models/view_mg.obj") == 0) return "xmodel/viewmodel_m1garand";
    if (cod_strcasecmp(name, "models/weapon_mg.obj") == 0) return "xmodel/weapon_m1garand";
    if (cod_strcasecmp(name, "models/view_sg.obj") == 0) return "xmodel/viewmodel_springfield";
    if (cod_strcasecmp(name, "models/weapon_sg.obj") == 0) return "xmodel/weapon_springfield";
    return NULL;
}

/* lightingPrecalc is an explicit shader generator, not a property of every
 * static model. Most vehicle skins use lightingDiffuse even when the entity
 * also carries a lightingPrecalc sample. Keep this choice per OBJ surface. */
static int export_model_lighting(const CodArchive* ar,unsigned char** data,unsigned* size){
    CodEmit e;const char* at=(const char*)*data;const char* end=at+*size;
    char types[64][128];int values[64],type_count=0;
    if(!cod_emit_init(&e))return 0;
    while(at<end){
        const char* nl=memchr(at,'\n',(size_t)(end-at));size_t len=nl?(size_t)(nl-at)+1:(size_t)(end-at);
        if(!cod_emit_fmt(&e,"%.*s",(int)len,at))goto bad;
        if(len>7 && !memcmp(at,"usemtl ",7)){
            char type[128],path[192];const char* name=at+7;const char* sep=memchr(name,'@',len-7);
            int precalc=0;unsigned char* raw=NULL;unsigned n=0;
            if(sep && sep-name>6 && !memcmp(name,"skins/",6) && sep-name-6<(int)sizeof(type)){
                size_t count=(size_t)(sep-name-6);memcpy(type,name+6,count);type[count]=0;
                int cached;for(cached=0;cached<type_count;cached++)if(!strcmp(types[cached],type))break;
                if(cached<type_count)precalc=values[cached];
                snprintf(path,sizeof(path),"shadertypes/model/%s.stype",type);
                if(cached==type_count && cod_archive_read(ar,path,&raw,&n)){
                    char* text=(char*)malloc((size_t)n+1);
                    if(!text){free(raw);goto bad;}memcpy(text,raw,n);text[n]=0;
                    {char* q=text;int rgb=0;while(*q){char word[64];int count=0;
                        while(*q && (isspace((unsigned char)*q) || *q=='{' || *q=='}'))q++;
                        if(q[0]=='/' && q[1]=='/'){while(*q && *q!='\n')q++;continue;}
                        if(q[0]=='/' && q[1]=='*'){q+=2;while(*q && !(q[0]=='*' && q[1]=='/'))q++;if(*q)q+=2;continue;}
                        if(!*q)break;
                        while(*q && !isspace((unsigned char)*q) && *q!='{' && *q!='}') {if(count<63)word[count++]=*q;q++;}word[count]=0;
                        if(rgb){precalc=!cod_strcasecmp(word,"lightingPrecalc");break;}
                        rgb=!cod_strcasecmp(word,"rgbGen");
                    }}free(text);free(raw);
                }
                if(cached==type_count && type_count<64){strcpy(types[type_count],type);values[type_count++]=precalc;}
            }
            if(!cod_emit_add(&e,precalc?"lighting baked\n":"lighting runtime\n"))goto bad;
        }
        at+=len;
    }
    {unsigned char* out=NULL;unsigned n=0;if(!cod_emit_take(&e,&out,&n))goto bad;free(*data);*data=out;*size=n;return 1;}
bad:cod_emit_free(&e);return 0;
}

static int xmodel_export_lod(const CodArchive* ar, const char* name,const char* idle,const char* up,int gun,int requested_lod,
                unsigned char** out_buf, unsigned int* out_size) {
    unsigned char* xm = NULL;
    unsigned char* sf = NULL;
    unsigned char* pt = NULL;
    unsigned int xsz = 0, ssz = 0, psz = 0;
    CodXmodelLod slots[4];
    char surf[300];
    char parts[300];
    int nslots, i, ok;
    unsigned char* best = NULL;
    unsigned best_sz = 0;

    if (!ar || !name || !out_buf || !out_size) return 0;
    if (strncmp(name, "xmodel/", 7) != 0 || !name[7]) return 0;
    if (strchr(name + 7, '/') || strchr(name + 7, '\\')) return 0;

    if (!cod_archive_read(ar, name, &xm, &xsz)) return 0;
    if (cod_xmodel_is_text(xm, xsz)) {
        ok = cod_xmodel_to_obj(xm, xsz, NULL, 0, NULL, 0, out_buf, out_size);
        free(xm);
        return ok;
    }
    nslots = cod_xmodel_lod_slots(xm, xsz, slots, 4);
    if (nslots <= 0) {
        free(xm);
        return 0;
    }
    cod_xmodel_set_keep_hands(strstr(name, "viewmodel") != NULL);
    if(idle){pose_weapon(ar,idle,up);if(g_motion_anim)cod_xmodel_overlay_anim_frame(g_motion_anim,g_motion_size,g_motion_frame);if(gun)cod_xmodel_remove_anim_bone("tag_weapon");}
    else if (strstr(name, "viewmodel_hands_whermact"))
        pose_mp44_hands(ar);
    else if(!strcmp(name,"xmodel/viewmodel_mp44")){
        pose_mp44_hands(ar);
        /* The hands attachment already supplies tag_weapon in world space. */
        cod_xmodel_remove_anim_bone("tag_weapon");
    }
    else
        cod_xmodel_set_anim_pose(NULL, 0);
    for (i = requested_lod<0?0:requested_lod; i < nslots && (requested_lod<0 || i==requested_lod); i++) {
        unsigned char* obj = NULL;
        unsigned osz = 0;
        if (!slots[i].name[0]) continue;
        snprintf(surf, sizeof(surf), "xmodelsurfs/%s", slots[i].name);
        if (!cod_archive_read(ar, surf, &sf, &ssz)) continue;
        snprintf(parts, sizeof(parts), "xmodelparts/%s", slots[i].name);
        if (!cod_archive_read(ar, parts, &pt, &psz)) {
            pt = NULL;
            psz = 0;
        }
        cod_xmodel_set_export_rig(strstr(name,"viewmodel")==NULL && psz>=6);
        ok = cod_xmodel_to_obj(xm, xsz, sf, ssz, pt, psz, &obj, &osz);
        cod_xmodel_set_export_rig(0);
        free(pt);
        free(sf);
        pt = NULL;
        sf = NULL;
        if (!ok || !obj) {
            free(obj);
            continue;
        }
        best = obj;
        best_sz = osz;
        break;
    }
    cod_xmodel_set_keep_hands(0);
    cod_xmodel_set_anim_pose(NULL, 0);
    free(xm);
    if (!best) return 0;
    if(!export_model_lighting(ar,&best,&best_sz)){free(best);return 0;}
    if (strstr(name, "viewmodel")) rewrite_view_basis(&best, &best_sz);
    if(requested_lod<0 && !strstr(name,"viewmodel")){
        CodEmit metadata;
        if(!cod_emit_init(&metadata)){free(best);return 0;}
        if(!cod_emit_fmt(&metadata,"%.*s",(int)best_sz,best)){cod_emit_free(&metadata);free(best);return 0;}
        for(i=1;i<nslots;i++)if(slots[i].name[0]){
            if(!cod_emit_fmt(&metadata,"\n# hx_lod %g %s.hxlod%d\n",slots[i-1].distance>0?slots[i-1].distance:(float)(i*600),name,i)){cod_emit_free(&metadata);free(best);return 0;}
        }
        free(best);if(!cod_emit_take(&metadata,&best,&best_sz)){cod_emit_free(&metadata);return 0;}
    }
    *out_buf = best;
    *out_size = best_sz;
    return 1;
}

static int xmodel_to_helix_posed(const CodArchive* ar,const char* name,const char* idle,const char* up,int gun,unsigned char** out,unsigned* size){return xmodel_export_lod(ar,name,idle,up,gun,-1,out,size);}
int xmodel_to_helix(const CodArchive* ar,const char* name,unsigned char** out,unsigned* size){return xmodel_to_helix_posed(ar,name,NULL,NULL,0,out,size);}
int xmodel_lod_to_helix(const CodArchive* ar,const char* name,int lod,unsigned char** out,unsigned* size){return lod>=1 && lod<4?xmodel_export_lod(ar,name,NULL,NULL,0,lod,out,size):0;}

static float view_weapon_number(const unsigned char* bytes,unsigned int size,const char* key,float fallback) {
    const char* p;const char* end;if(!bytes)return fallback;p=(const char*)bytes;end=p+size;
    while(p<end){const char* name;size_t n;if(*p!='\\'){p++;continue;}name=++p;while(p<end && *p!='\\')p++;n=(size_t)(p-name);if(p>=end)break;p++;
        {const char* value=p;char text[64];size_t v;while(p<end && *p!='\\')p++;v=(size_t)(p-value);
         if(n==strlen(key) && !memcmp(name,key,n) && v<sizeof(text)){memcpy(text,value,v);text[v]=0;return (float)atof(text);}}
    }
    return fallback;
}

/* Publish the native rigid torso motion, without baking new geometry per frame. */
static int viewhand_ads_profile(const CodArchive* ar,const unsigned char* parts,unsigned int parts_size,const char* weapon_path,const char* idle,const char* up,const char* down,unsigned char** out,unsigned int* size) {
    unsigned char* weapon=NULL;unsigned int wsz=0;CodEmit e;float hip[12];int side;
    if(!cod_xmodel_view_tag(parts,parts_size,"tag_torso",hip) || !cod_emit_init(&e))return 0;
    cod_archive_read(ar,weapon_path,&weapon,&wsz);
    cod_emit_fmt(&e,"ads_config %.9g %.9g %.9g %.9g\n",view_weapon_number(weapon,wsz,"adsTransInTime",.35f),view_weapon_number(weapon,wsz,"adsTransOutTime",.4f),view_weapon_number(weapon,wsz,"adsZoomFov",55),view_weapon_number(weapon,wsz,"adsViewBobMult",.2f));free(weapon);
    for(side=0;side<2;side++){
        unsigned char* anim=NULL;unsigned int count=0;unsigned frames,i;const char* name=side?down:up;
        if(!cod_archive_read(ar,name,&anim,&count) || count<9){free(anim);cod_emit_free(&e);return 0;}
        frames=anim[2]|(anim[3]<<8);if(frames<2 || frames>256){free(anim);cod_emit_free(&e);return 0;}
        for(i=0;i<frames;i++){
            float tag[12],delta[12];int row,col,k;
            pose_weapon(ar,idle,up);
            if(!cod_xmodel_overlay_anim_frame(anim,count,i) || !cod_xmodel_view_tag(parts,parts_size,"tag_torso",tag)){free(anim);cod_emit_free(&e);return 0;}
            for(col=0;col<3;col++)for(row=0;row<3;row++){float v=0;for(k=0;k<3;k++)v+=tag[k*3+row]*hip[k*3+col];delta[col*3+row]=v;}
            for(row=0;row<3;row++)delta[9+row]=tag[9+row]-(delta[row]*hip[9]+delta[3+row]*hip[10]+delta[6+row]*hip[11]);
            cod_emit_fmt(&e,"ads_%s %.9g",side?"down":"up",(double)i/(frames-1));
            for(k=0;k<12;k++)cod_emit_fmt(&e," %.9g",delta[k]);cod_emit_add(&e,"\n");
        }
        free(anim);
    }
    {unsigned char* grown=(unsigned char*)realloc(*out,*size+e.len+1);if(!grown){cod_emit_free(&e);return 0;}memcpy(grown+*size,e.data,e.len+1);*out=grown;*size+=(unsigned int)e.len;}
    cod_emit_free(&e);return 1;
}

int cod_viewhand_file(const CodArchive* ar, unsigned char** out_buf, unsigned int* out_size) {
    unsigned char* xm = NULL;
    unsigned xsz = 0;
    unsigned char* pt = NULL;
    unsigned psz = 0;
    CodXmodelLod slots[4];
    int nslots, i;
    float tag[12];
    char parts_name[96];
    if (!ar || !out_buf || !out_size) return 0;
    if (!cod_archive_contains(ar, "xmodel/viewmodel_mp44")) return 0;
    if (!cod_archive_read(ar, "xmodel/viewmodel_hands_whermact", &xm, &xsz)) return 0;
    nslots = cod_xmodel_lod_slots(xm, xsz, slots, 4);
    free(xm);
    pose_mp44_hands(ar);
    for (i = 0; i < nslots; i++) {
        if (!slots[i].name[0]) continue;
        snprintf(parts_name, sizeof(parts_name), "xmodelparts/%s", slots[i].name);
        if (!cod_archive_read(ar, parts_name, &pt, &psz)) continue;
        if (cod_xmodel_view_tag(pt, psz, "tag_weapon", tag)) {
            int ok=cod_viewhand_text(tag,out_buf,out_size);
            if(ok)viewhand_ads_profile(ar,pt,psz,"weapons/mp/mp44_mp","xanim/viewmodel_mp44_idle","xanim/viewmodel_mp44_ADS_up","xanim/viewmodel_mp44_ADS_down",out_buf,out_size);
            free(pt);cod_xmodel_set_anim_pose(NULL,0);return ok;
        }
        free(pt);
        pt = NULL;
    }
    cod_xmodel_set_anim_pose(NULL, 0);
    return 0;
}

int cod_model_tags(const CodArchive* ar,const char* model,unsigned char** out,unsigned int* size) {
    unsigned char *xm=NULL,*pt=NULL;unsigned int xs=0,ps=0;CodXmodelLod slots[4];char path[256],text[1024];int n,i,used=0;
    const char* names[]={"tag_aim","tag_butt","tag_flash"};
    if(!cod_archive_read(ar,model,&xm,&xs))return 0;
    n=cod_xmodel_lod_slots(xm,xs,slots,4);free(xm);if(n<=0)return 0;
    snprintf(path,sizeof(path),"xmodelparts/%s",slots[0].name);
    if(!cod_archive_read(ar,path,&pt,&ps))return 0;
    cod_xmodel_set_anim_pose(NULL,0);
    for(i=0;i<3;i++){float tag[12];if(cod_xmodel_view_tag(pt,ps,names[i],tag))used+=snprintf(text+used,sizeof(text)-used,"%s %.9g %.9g %.9g\n",names[i],tag[11],-tag[9],tag[10]);}
    free(pt);if(!used)return 0;*out=(unsigned char*)malloc(used+1);if(!*out)return 0;memcpy(*out,text,used+1);*size=used;return 1;
}

/* Native weapon definitions are translated here, not in the host engine. */
static const char* arsenal_names[] = {
    "m1garand_mp", "m1carbine_mp", "thompson_mp", "bar_mp", "springfield_mp", "colt_mp",
    "kar98k_mp", "mp40_mp", "mp44_mp", "kar98k_sniper_mp", "luger_mp",
    "enfield_mp", "sten_mp", "bren_mp", "mosin_nagant_mp", "ppsh_mp", "mosin_nagant_sniper_mp",
    "fraggrenade_mp", "stielhandgranate_mp", "mk1britishfrag_mp", "rgd-33russianfrag_mp",
    "panzerfaust_mp", "fg42_mp", "thompson_semi_mp", "bar_slow_mp", "mp44_semi_mp", "ppsh_semi_mp", "fg42_semi_mp"
};
static int arsenal_index(const char* name) {
    unsigned i;for(i=0;i<sizeof(arsenal_names)/sizeof(arsenal_names[0]);i++)
        if(!strcmp(name,arsenal_names[i]))return (int)i;
    return -1;
}
static void weapon_value(const unsigned char* raw,unsigned size,const char* key,char* value,size_t cap) {
    const char *p=(const char*)raw,*end=p+size;value[0]=0;
    while(p<end){const char *name,*start;size_t n,v;
        if(*p++!='\\')continue;name=p;while(p<end&&*p!='\\')p++;n=(size_t)(p-name);
        if(p>=end)break;start=++p;while(p<end&&*p!='\\')p++;v=(size_t)(p-start);
        if(n==strlen(key)&&!memcmp(name,key,n)){if(v>=cap)v=cap-1;memcpy(value,start,v);value[v]=0;return;}
    }
}
static void weapon_asset(const unsigned char* raw,unsigned size,const char* key,const char* prefix,char* out,size_t cap) {
    char value[192];weapon_value(raw,size,key,value,sizeof(value));
    if(value[0]&&strcmp(value,"none"))snprintf(out,cap,"%s%s",strncmp(value,prefix,strlen(prefix))?prefix:"",value);else out[0]=0;
}
static float fx_number(const char* block,const char* key,float fallback){
    const char* p=block;size_t n=strlen(key);while((p=strstr(p,key))!=NULL){if((p==block||p[-1]=='\n'||p[-1]=='\r'||p[-1]=='\t'||p[-1]==' ')&&(p[n]=='\t'||p[n]==' ')){float value;if(sscanf(p+n,"%f",&value)==1)return value;}p+=n;}return fallback;
}
static void weapon_particles(const CodArchive* ar,CodEmit* out,const unsigned char* config,unsigned size,const char* type){
    char name[256],path[300];unsigned char* raw=NULL;unsigned n=0;char *text,*p;
    if(strcmp(type,"grenade")&&strcmp(type,"projectile"))return;
    weapon_value(config,size,"projExplosionEffect",name,sizeof(name));
    if(!name[0] || !strcmp(name,"none"))strcpy(name,"explosions/grenade1");
    snprintf(path,sizeof(path),"%s%s%s",!strncmp(name,"fx/",3)?"":"fx/",name,strstr(name,".efx")?"":".efx");
    if(!cod_archive_read(ar,path,&raw,&n))return;text=(char*)malloc(n+1);if(!text){free(raw);return;}memcpy(text,raw,n);text[n]=0;free(raw);p=text;
    while((p=strstr(p,"Particle"))!=NULL){
        char *begin=strchr(p,'{'),*end,*shaders,*sizeblock;int depth=0,count;float life,start,end_size,speed,gravity;
        if(!begin)break;for(end=begin;*end;end++){if(*end=='{')depth++;if(*end=='}'&&!--depth)break;}if(!*end)break;
        *end=0;shaders=strstr(begin,"shaders");sizeblock=strstr(begin,"size");
        life=fx_number(begin,"life",300);count=(int)fx_number(begin,"count",1);speed=fx_number(begin,"velocity",40);gravity=fx_number(begin,"gravity",0);
        start=sizeblock?fx_number(sizeblock,"start",10):10;end_size=sizeblock?fx_number(sizeblock,"end",40):40;
        if(shaders){char shader[192];shaders=strchr(shaders,'[');if(shaders){shaders++;while(*shaders && (*shaders==' '||*shaders=='\t'||*shaders=='\r'||*shaders=='\n'))shaders++;if(sscanf(shaders,"%191s",shader)==1 && count>0)cod_emit_fmt(out,"particle { material \"%s\" blend \"%s\" count %d lifetime %.9g startSize %.9g endSize %.9g speed %.9g gravity %.9g }\n",shader,strstr(begin,"useAlpha")?"alpha":"additive",count,life*.001f,start,end_size,speed,gravity);}}
        p=end+1;
    }
    free(text);
}

int cod_weapon_catalog(const CodArchive* ar,unsigned char** output,unsigned* size) {
    CodEmit out;unsigned i;if(!cod_emit_init(&out))return 0;
    for(i=0;i<sizeof(arsenal_names)/sizeof(arsenal_names[0]);i++) {
        unsigned char* raw=NULL;unsigned n=0;char path[128],display[160],type[32],class_name[32],sound[160],scope[192],world[192],up[192];
        snprintf(path,sizeof(path),"weapons/mp/%s",arsenal_names[i]);
        if(!cod_archive_read(ar,path,&raw,&n))continue;
        weapon_value(raw,n,"displayName",display,sizeof(display));weapon_value(raw,n,"weaponType",type,sizeof(type));weapon_value(raw,n,"weaponClass",class_name,sizeof(class_name));
        weapon_value(raw,n,"fireSound",sound,sizeof(sound));weapon_value(raw,n,"adsOverlayShader",scope,sizeof(scope));if(!strcmp(scope,"none"))scope[0]=0;if(!strcmp(sound,"none"))sound[0]=0;
        weapon_asset(raw,n,"worldModel","xmodel/",world,sizeof(world));weapon_value(raw,n,"adsUpAnim",up,sizeof(up));
        cod_emit_fmt(&out,"weapon { id %u name \"%s\" display \"%s\" slot %d type %d world \"%s\" sound \"%s\" scope \"%s\" ads %d\n",
            16+i,arsenal_names[i],display,!strcmp(class_name,"pistol")?1:!strcmp(type,"grenade")?2:0,
            !strcmp(type,"grenade")?2:!strcmp(type,"projectile")?1:0,world,sound,scope,up[0]!=0);
        cod_emit_fmt(&out,"class \"%s\"\n",class_name);
        {const char* keys[]={"damage","meleeDamage","meleeTime","fireTime","semiAuto","clipSize","maxAmmo","startAmmo","dropTime","raiseTime","altDropTime","altRaiseTime","reloadTime","reloadEmptyTime","rechamberTime","hipSpreadStandMin","adsSpread","projectileSpeed","explosionRadius","explosionInnerDamage","fuseTime","adsTransInTime","adsTransOutTime","adsZoomFov","segmentedReload","reloadStartTime","clipOnly"};unsigned k;
        for(k=0;k<sizeof(keys)/sizeof(keys[0]);k++)cod_emit_fmt(&out,"%s %.9g\n",keys[k],view_weapon_number(raw,n,keys[k],0));}
        {char reticle[64];weapon_value(raw,n,"adsOverlayReticle",reticle,sizeof(reticle));cod_emit_fmt(&out,"reticle \"%s\"\n",reticle);}
        {char alternate[64];const char* pose=arsenal_names[i];weapon_value(raw,n,"altWeapon",alternate,sizeof(alternate));
         if(i>=23){static const char* base[]={"thompson_mp","bar_mp","mp44_mp","ppsh_mp","fg42_mp"};pose=base[i-23];}
         cod_emit_fmt(&out,"alternate \"%s\" pose \"%s\"\n",alternate,pose);}
        {char projectile[192],sound[192];weapon_asset(raw,n,"projectileModel","xmodel/",projectile,sizeof(projectile));weapon_value(raw,n,"explosionSound",sound,sizeof(sound));if(!projectile[0] && !strcmp(type,"grenade"))snprintf(projectile,sizeof(projectile),"%s",world);cod_emit_fmt(&out,"projectile \"%s\" explosionSound \"%s\"\n",projectile,sound);}
        {char sound[192];weapon_value(raw,n,"reloadSound",sound,sizeof(sound));cod_emit_fmt(&out,"reloadSound \"%s\"\n",sound);weapon_value(raw,n,"reloadEmptySound",sound,sizeof(sound));cod_emit_fmt(&out,"reloadEmptySound \"%s\"\n",sound);}
        cod_emit_fmt(&out,"disposable %d\n",!strcmp(arsenal_names[i],"panzerfaust_mp"));
        weapon_particles(ar,&out,raw,n,type);
        cod_emit_add(&out,"}\n");free(raw);
    }
    return cod_emit_take(&out,output,size);
}
typedef struct MotionBuffer {unsigned char* data;size_t len,cap;} MotionBuffer;
static int motion_add(MotionBuffer* b,const void* data,size_t n){
    unsigned char* p;size_t cap;if(n>256u*1024u*1024u || b->len>256u*1024u*1024u-n)return 0;
    if(b->len+n>b->cap){cap=(b->len+n)*2+4096;p=(unsigned char*)realloc(b->data,cap);if(!p)return 0;b->data=p;b->cap=cap;}
    memcpy(b->data+b->len,data,n);b->len+=n;return 1;
}
static int motion_mesh(MotionBuffer* b,const unsigned char* bytes,unsigned length){
    const char *p=(const char*)bytes,*end=p+length;unsigned positions=0,normals=0,faces=0,pi=0,ni=0;float *pos=NULL,*norm=NULL;
    while(p<end){if(p[0]=='v' && p[1]==' ')positions++;else if(p[0]=='v'&&p[1]=='n'&&p[2]==' ')normals++;else if(p[0]=='f'&&p[1]==' ')faces++;p=strchr(p,'\n');if(!p)break;p++;}
    if(!positions || !normals || !faces)return 0;pos=(float*)malloc(positions*3*sizeof(float));norm=(float*)malloc(normals*3*sizeof(float));if(!pos||!norm)goto bad;
    p=(const char*)bytes;while(p<end){if(p[0]=='v' && p[1]==' '){if(sscanf(p+2,"%f %f %f",pos+pi*3,pos+pi*3+1,pos+pi*3+2)!=3)goto bad;pi++;}else if(p[0]=='v'&&p[1]=='n'&&p[2]==' '){if(sscanf(p+3,"%f %f %f",norm+ni*3,norm+ni*3+1,norm+ni*3+2)!=3)goto bad;ni++;}p=strchr(p,'\n');if(!p)break;p++;}
    {unsigned count=faces*3;if(!motion_add(b,&count,4))goto bad;}
    p=(const char*)bytes;while(p<end){if(p[0]=='f'&&p[1]==' '){int v[3],t[3],n[3],k;if(sscanf(p+2,"%d/%d/%d %d/%d/%d %d/%d/%d",v,t,n,v+1,t+1,n+1,v+2,t+2,n+2)!=9)goto bad;for(k=0;k<3;k++){if(v[k]<1||n[k]<1||(unsigned)v[k]>positions||(unsigned)n[k]>normals)goto bad;if(!motion_add(b,pos+(v[k]-1)*3,12)||!motion_add(b,norm+(n[k]-1)*3,12))goto bad;}}p=strchr(p,'\n');if(!p)break;p++;}
    free(pos);free(norm);return 1;
bad:free(pos);free(norm);return 0;
}

static int weapon_motion(const CodArchive* ar,const unsigned char* config,unsigned size,const char* hands,const char* gun,const char* idle,const char* up,unsigned char** output,unsigned* length){
    static const char* keys[]={"reloadAnim","reloadEmptyAnim","reloadStartAnim","reloadEndAnim","fireAnim","adsFireAnim","holdFireAnim","dropAnim","raiseAnim","altDropAnim","altRaiseAnim","meleeAnim"};
    static const char* names[]={"reload","reloadempty","reloadstart","reloadend","fire","adsfire","pin","drop","raise","altdrop","altraise","melee"};
    MotionBuffer b={0};char prior[12][256]={{0}};unsigned clips=0,k;int ok=0;unsigned char *parts=NULL,*model=NULL;unsigned pn=0,mn=0;CodXmodelLod lod[4];int count;
    if(!motion_add(&b,"HXVM2\0\0\0",8)||!motion_add(&b,&clips,4))goto done;
    if(!cod_archive_read(ar,hands,&model,&mn))goto done;count=cod_xmodel_lod_slots(model,mn,lod,4);free(model);model=NULL;
    if(count<=0)goto done;
    {char path[256];snprintf(path,sizeof(path),"xmodelparts/%s",lod[0].name);if(!cod_archive_read(ar,path,&parts,&pn))goto done;}
    for(k=0;k<12;k++){
        unsigned char* anim=NULL;unsigned an=0,total,frames,f;float duration;char path[256],label[16]={0};
        weapon_asset(config,size,keys[k],"xanim/",path,sizeof(path));if(!path[0]||!cod_archive_read(ar,path,&anim,&an))continue;
        if(an<9 || anim[0]!=14 || anim[1]!=0){free(anim);continue;}
        total=anim[2]|((unsigned)anim[3]<<8);if(total<2){free(anim);continue;}
        duration=(float)(total-1)/(float)((anim[7]|((unsigned)anim[8]<<8))?anim[7]|((unsigned)anim[8]<<8):30);
        frames=(unsigned)(duration*15.f)+2;if(frames>48)frames=48;if(frames>total)frames=total;snprintf(label,sizeof(label),"%s",names[k]);
        {unsigned prev;for(prev=0;prev<k;prev++)if(prior[prev][0]&&!strcmp(prior[prev],path)){char alias[16]={0};unsigned zero=0;snprintf(alias,sizeof(alias),"%s",names[prev]);if(!motion_add(&b,label,16)||!motion_add(&b,&duration,4)||!motion_add(&b,&zero,4)||!motion_add(&b,alias,16)){free(anim);goto done;}free(anim);clips++;break;}if(prev<k)continue;}
        snprintf(prior[k],sizeof(prior[k]),"%s",path);
        if(!motion_add(&b,label,16)||!motion_add(&b,&duration,4)||!motion_add(&b,&frames,4)){free(anim);goto done;}
        g_motion_anim=anim;g_motion_size=an;
        for(f=0;f<frames;f++){
            unsigned char *h=NULL,*g=NULL;unsigned hn=0,gn=0;float tag[12];
            g_motion_frame=f*(total-1)/(frames-1);
            pose_weapon(ar,idle,up[0]?up:NULL);
            if(!cod_xmodel_overlay_anim_frame(anim,an,g_motion_frame)||!cod_xmodel_view_tag(parts,pn,"tag_weapon",tag))goto frame_bad;
            if(!xmodel_to_helix_posed(ar,hands,idle,up[0]?up:NULL,0,&h,&hn)||!xmodel_to_helix_posed(ar,gun,idle,up[0]?up:NULL,1,&g,&gn))goto frame_bad;
            if(!motion_add(&b,tag,sizeof(tag))||!motion_mesh(&b,h,hn)||!motion_mesh(&b,g,gn))goto frame_bad;
            free(h);free(g);continue;
frame_bad:
            free(h);free(g);free(anim);goto done;
        }
        free(anim);g_motion_anim=NULL;clips++;
    }
    memcpy(b.data+8,&clips,4);*output=b.data;*length=(unsigned)b.len;b.data=NULL;ok=1;
done:
    g_motion_anim=NULL;g_motion_size=g_motion_frame=0;cod_xmodel_set_anim_pose(NULL,0);free(model);free(parts);free(b.data);return ok;
}

static int viewhand_path(const char* path,char* weapon,int* nation,char* asset) {
    int used=0;
    if(strncmp(path,"cod/viewhands/",14)||sscanf(path+14,"%63[^/]/%d/%31s%n",weapon,nation,asset,&used)!=3 || path[14+used] || *nation<1 || *nation>13)return 0;
    return arsenal_index(weapon)>=0 && (!strcmp(asset,"profile.txt")||!strcmp(asset,"hands.obj")||!strcmp(asset,"gun.obj")||!strcmp(asset,"motion.bin"));
}
int cod_weapon_view_exists(const CodArchive* ar,const char* path) {
    char weapon[64],asset[32],source[128];int nation;
    if(!viewhand_path(path,weapon,&nation,asset))return 0;
    snprintf(source,sizeof(source),"weapons/mp/%s",weapon);return cod_archive_contains(ar,source);
}
int cod_weapon_view_file(const CodArchive* ar,const char* path,unsigned char** output,unsigned* size) {
    static const char* hands[]={"","xmodel/viewmodel_hands_us","xmodel/viewmodel_hands_british","xmodel/viewmodel_hands_russian","xmodel/viewmodel_hands_whermact","xmodel/viewmodel_hands_uswinter","xmodel/viewmodel_hands_waffen","xmodel/viewmodel_hands_whermact_winter","xmodel/viewmodel_hands_waffen_winter","xmodel/viewmodel_hands_russian_vetrn","xmodel/viewmodel_hands_british_air","xmodel/viewmodel_hands_fallschirmjager","xmodel/viewmodel_hands_fallschirmjager_grey","xmodel/viewmodel_hands_kriegsmarine"};
    char weapon[64],asset[32],source[128],idle[256],up[256],down[256],gun[256];int nation;
    unsigned char *raw=NULL,*xm=NULL,*parts=NULL;unsigned n=0,xn=0,pn=0;CodXmodelLod lod[4];
    int count,i,ok=0;float tag[12];
    if(!viewhand_path(path,weapon,&nation,asset))return 0;
    snprintf(source,sizeof(source),"weapons/mp/%s",weapon);if(!cod_archive_read(ar,source,&raw,&n))return 0;
    weapon_asset(raw,n,"idleAnim","xanim/",idle,sizeof(idle));weapon_asset(raw,n,"adsUpAnim","xanim/",up,sizeof(up));weapon_asset(raw,n,"adsDownAnim","xanim/",down,sizeof(down));weapon_asset(raw,n,"gunModel","xmodel/",gun,sizeof(gun));
    if(!strcmp(asset,"motion.bin")){ok=weapon_motion(ar,raw,n,hands[nation],gun,idle,up,output,size);free(raw);return ok;}
    if(strcmp(asset,"profile.txt")) {
        ok=xmodel_to_helix_posed(ar,!strcmp(asset,"hands.obj")?hands[nation]:gun,idle,up[0]?up:NULL,!strcmp(asset,"gun.obj"),output,size);
        free(raw);return ok;
    }
    if(!cod_archive_read(ar,hands[nation],&xm,&xn)){free(raw);return 0;}
    count=cod_xmodel_lod_slots(xm,xn,lod,4);free(xm);pose_weapon(ar,idle,up[0]?up:NULL);
    for(i=0;i<count;i++) {
        char part_path[256];CodEmit e;
        snprintf(part_path,sizeof(part_path),"xmodelparts/%s",lod[i].name);
        if(!cod_archive_read(ar,part_path,&parts,&pn))continue;
        if(cod_xmodel_view_tag(parts,pn,"tag_weapon",tag)&&cod_emit_init(&e)) {
            int k;cod_emit_fmt(&e,"hands cod/viewhands/%s/%d/hands.obj\ngun cod/viewhands/%s/%d/gun.obj\ntag",weapon,nation,weapon,nation);
            for(k=0;k<12;k++)cod_emit_fmt(&e," %.9g",tag[k]);cod_emit_add(&e,"\n");cod_emit_fmt(&e,"bank_nation %d\n",nation);
            ok=cod_emit_take(&e,output,size);
            if(ok&&up[0]&&down[0])viewhand_ads_profile(ar,parts,pn,source,idle,up,down,output,size);
        }
        free(parts);parts=NULL;if(ok)break;
    }
    free(raw);cod_xmodel_set_anim_pose(NULL,0);return ok;
}

int cod_weapon_view_checksum(const CodArchive* ar,const char* path,unsigned* checksum) {
    static const char* hands[]={"","xmodel/viewmodel_hands_us","xmodel/viewmodel_hands_british","xmodel/viewmodel_hands_russian","xmodel/viewmodel_hands_whermact","xmodel/viewmodel_hands_uswinter","xmodel/viewmodel_hands_waffen","xmodel/viewmodel_hands_whermact_winter","xmodel/viewmodel_hands_waffen_winter","xmodel/viewmodel_hands_russian_vetrn","xmodel/viewmodel_hands_british_air","xmodel/viewmodel_hands_fallschirmjager","xmodel/viewmodel_hands_fallschirmjager_grey","xmodel/viewmodel_hands_kriegsmarine"};
    char name[64],asset[32],source[128],paths[17][256];int nation,i;unsigned crc,hash=2166136261u^0x434D5034u,n=0;unsigned char* raw=NULL;
    if(!viewhand_path(path,name,&nation,asset))return 0;
    snprintf(source,sizeof(source),"weapons/mp/%s",name);
    if(!cod_archive_checksum(ar,source,&crc)||!cod_archive_read(ar,source,&raw,&n))return 0;
    hash=(hash^crc)*16777619u;
    snprintf(paths[0],sizeof(paths[0]),"%s",hands[nation]);weapon_asset(raw,n,"gunModel","xmodel/",paths[1],sizeof(paths[1]));
    weapon_asset(raw,n,"idleAnim","xanim/",paths[2],sizeof(paths[2]));weapon_asset(raw,n,"adsUpAnim","xanim/",paths[3],sizeof(paths[3]));weapon_asset(raw,n,"adsDownAnim","xanim/",paths[4],sizeof(paths[4]));
    {const char* keys[]={"reloadAnim","reloadEmptyAnim","reloadStartAnim","reloadEndAnim","fireAnim","adsFireAnim","holdFireAnim","dropAnim","raiseAnim","altDropAnim","altRaiseAnim","meleeAnim"};for(i=0;i<12;i++)weapon_asset(raw,n,keys[i],"xanim/",paths[5+i],sizeof(paths[5+i]));}free(raw);
    for(i=0;i<17;i++)if(paths[i][0]){
        if(cod_archive_checksum(ar,paths[i],&crc))hash=(hash^crc)*16777619u;
        if(i<2){unsigned char* model=NULL;unsigned size=0;CodXmodelLod lod[4];int j,count;
            if(!cod_archive_read(ar,paths[i],&model,&size))continue;
            count=cod_xmodel_lod_slots(model,size,lod,4);free(model);
            for(j=0;j<count;j++)if(lod[j].name[0]){char dependency[256];snprintf(dependency,sizeof(dependency),"xmodelparts/%s",lod[j].name);if(cod_archive_checksum(ar,dependency,&crc))hash=(hash^crc)*16777619u;snprintf(dependency,sizeof(dependency),"xmodelsurfs/%s",lod[j].name);if(cod_archive_checksum(ar,dependency,&crc))hash=(hash^crc)*16777619u;}
        }
    }
    if(!strcmp(asset,"motion.bin"))hash^=0x4D4F5434u;
    *checksum=hash;return 1;
}
