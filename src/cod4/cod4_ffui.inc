/* Private IW3 compiled MenuList reader. Included after the shared zone helpers.
 * The output uses Helix's text menu format; IW3 expressions and UIScript remain
 * engine-specific and are deliberately not executed by this asset reader. */
typedef struct UiLocalizedText { char *key,*value; } UiLocalizedText;
typedef struct UiLocalization { UiLocalizedText* entries;unsigned count;struct UiLocalization* materials; } UiLocalization;
static void ui_loc_free(UiLocalization* loc) {
    unsigned i;for(i=0;i<loc->count;i++){free(loc->entries[i].key);free(loc->entries[i].value);}
    free(loc->entries);memset(loc,0,sizeof(*loc));
}
static int ui_loc_store(UiLocalization* loc,const char* key,const char* value) {
    UiLocalizedText* grown;char *k,*v;size_t kn=strlen(key)+1,vn=strlen(value)+1;
    if(loc->count>=100000)return 0;
    k=(char*)malloc(kn);v=(char*)malloc(vn);
    if(!k || !v){free(k);free(v);return 0;}
    memcpy(k,key,kn);memcpy(v,value,vn);
    grown=(UiLocalizedText*)realloc(loc->entries,(loc->count+1u)*sizeof(*grown));
    if(!grown){free(k);free(v);return 0;}
    loc->entries=grown;grown[loc->count].key=k;grown[loc->count].value=v;loc->count++;
    return 1;
}
static const char* ui_loc_text(const UiLocalization* loc,const char* text) {
    unsigned i;
    if(!loc || !text || *text!='@')return text;
    for(i=loc->count;i>0;i--)if(!strcmp(loc->entries[i-1].key,text+1))return loc->entries[i-1].value;
    return text; /* Keep unresolved keys visible for diagnosis. */
}
static const char* ui_string(Cur* c,unsigned ptr) {
    unsigned i,offset;
    if(!ptr)return "";
    if(is_neg(ptr)) {
        const char* value;
        if(!remain(c,1))return NULL;
        value=(const char*)c->d+c->o;
        return xstr(c,ptr)?value:NULL;
    }
    if(!c->virtual_absolute || !c->world_valid || ((ptr-1u)>>28)!=4u)return c->ui_assets==2?"":NULL;
    offset=(ptr-1u)&0x0fffffffu;
    for(i=0;i<c->world_alloc_n;i++) {
        const WorldAlloc* a=c->world_allocs+i;
        if(a->offset==offset && a->string_offset!=FF_NEG1)return (const char*)c->d+a->string_offset;
    }
    return c->ui_assets==2?"":NULL;
}
static int ui_keys(Cur* c,unsigned ptr,unsigned depth) {
    const unsigned char* b;
    if(!is_neg(ptr))return 1;
    if(depth>128)return 0;
    world_alloc(c,4,12);
    b=take(c,12);
    return b && xstr(c,rd_u32(b+4)) && ui_keys(c,rd_u32(b+8),depth+1u);
}
typedef struct UiExprToken { unsigned offset,type,kind,value;const char* string; } UiExprToken;
typedef struct UiExprStatement { unsigned offset,count;UiExprToken* tokens; } UiExprStatement;
typedef struct UiExprState {
    UiExprToken* entries;unsigned entry_count,entry_cap;
    UiExprStatement* statements;unsigned count,cap;
} UiExprState;
static void ui_expr_free(Cur* c) {
    unsigned i;UiExprState* e=c->ui_expr;if(!e)return;
    for(i=0;i<e->count;i++)free(e->statements[i].tokens);
    free(e->entries);free(e->statements);free(e);c->ui_expr=NULL;
}
static const UiExprToken* ui_expr_entry(UiExprState* e,unsigned ptr) {
    unsigned lo=0,hi=e->entry_count,offset;
    if(ptr<=1u || ((ptr-1u)>>28)!=4u)return NULL;
    offset=(ptr-1u)&0x0fffffffu;
    while(lo<hi){unsigned m=lo+(hi-lo)/2;if(e->entries[m].offset<offset)lo=m+1;else hi=m;}
    return lo<e->entry_count && e->entries[lo].offset==offset?e->entries+lo:NULL;
}
static int ui_read_statement(Cur* c,const unsigned char* s,const UiExprStatement** result) {
    unsigned n=rd_u32(s),ptr=rd_u32(s+4),i,event,offset;
    const unsigned char* ptrs;UiExprState* e;UiExprStatement decoded;
    static UiExprToken unknown_token={0,2,0,0,NULL};
    static UiExprStatement unknown_statement={0,1,&unknown_token};
    *result=NULL;if(n>4096)return 0;if(!n)return 1;if(!ptr)return 0;
    if(!c->ui_expr){c->ui_expr=(UiExprState*)calloc(1,sizeof(UiExprState));if(!c->ui_expr)return 0;}
    e=c->ui_expr;
    if(!is_neg(ptr)) {
        unsigned lo=0,hi=e->count;
        *result=&unknown_statement;
        if(ptr<=1u || ((ptr-1u)>>28)!=4u)return 1;
        offset=(ptr-1u)&0x0fffffffu;
        while(lo<hi){unsigned m=lo+(hi-lo)/2;if(e->statements[m].offset<offset)lo=m+1;else hi=m;}
        if(lo<e->count && e->statements[lo].offset==offset) {
            if(e->statements[lo].count!=n)return 0;*result=e->statements+lo;
        }
        return 1;
    }
    event=world_alloc(c,4,n*4u);offset=event?c->world_allocs[event-1u].offset:0;
    ptrs=take(c,n*4u);if(!ptrs)return 0;
    decoded.offset=offset;decoded.count=n;decoded.tokens=(UiExprToken*)calloc(n,sizeof(UiExprToken));
    if(!decoded.tokens)return 0;
    for(i=0;i<n;i++) {
        UiExprToken* t=decoded.tokens+i;unsigned p=rd_u32(ptrs+i*4u);
        t->type=2; /* Explicit unresolved token; never fabricate an operand. */
        if(is_neg(p)) {
            const unsigned char* raw;UiExprToken* grown;
            event=world_alloc(c,4,12);raw=take(c,12);if(!raw)goto fail;
            t->offset=event?c->world_allocs[event-1u].offset:0;
            t->type=rd_u32(raw);t->kind=rd_u32(raw+4);t->value=rd_u32(raw+8);
            if(t->type==1 && t->kind==2) {
                t->string=ui_string(c,t->value);
                if(!t->string && is_neg(t->value))goto fail;
                if(!t->string)t->type=2;
            }
            if(e->entry_count==e->entry_cap) {
                unsigned cap=e->entry_cap?e->entry_cap*2:128;if(cap>1000000)goto fail;
                grown=(UiExprToken*)realloc(e->entries,cap*sizeof(*grown));if(!grown)goto fail;
                e->entries=grown;e->entry_cap=cap;
            }
            e->entries[e->entry_count++]=*t;
        } else {
            const UiExprToken* known=ui_expr_entry(e,p);if(known)*t=*known;
        }
    }
    if(e->count==e->cap) {
        unsigned cap=e->cap?e->cap*2:64;UiExprStatement* grown;
        if(cap>100000)goto fail;
        grown=(UiExprStatement*)realloc(e->statements,cap*sizeof(*grown));if(!grown)goto fail;
        e->statements=grown;e->cap=cap;
    }
    e->statements[e->count]=decoded;*result=e->statements+e->count++;return 1;
fail:
    free(decoded.tokens);return 0;
}
static int ui_quote(Buf* out,const char* s);
static int ui_statement_expr(Cur* c,const unsigned char* s,const char** literal,Buf* expression) {
    const UiExprStatement* stmt;unsigned i;char line[64];
    if(literal)*literal=NULL;
    if(!ui_read_statement(c,s,&stmt))return 0;
    if(!stmt)return 1;
    if(literal && stmt->count==2 && stmt->tokens[0].type==0 && stmt->tokens[0].kind==16 &&
       stmt->tokens[1].type==1 && stmt->tokens[1].kind==2)*literal=stmt->tokens[1].string;
    if(!expression)return 1;
    for(i=0;i<stmt->count;i++) {
        const UiExprToken* t=stmt->tokens+i;
        if(t->type==0)snprintf(line,sizeof(line)," op %u",t->kind);
        else if(t->type==1 && t->kind==0)snprintf(line,sizeof(line)," int %d",(int)t->value);
        else if(t->type==1 && t->kind==1) {
            float value;memcpy(&value,&t->value,4);
            if(finite_f(value))snprintf(line,sizeof(line)," float %.9g",value);else snprintf(line,sizeof(line)," unknown 0");
        } else if(t->type==1 && t->kind==2 && t->string) {
            if(!buf_str(expression," string ") || !ui_quote(expression,t->string))return 0;continue;
        } else snprintf(line,sizeof(line)," unknown 0");
        if(!buf_str(expression,line))return 0;
    }
    return 1;
}
static int ui_statement_text(Cur* c,const unsigned char* s,const char** literal) {
    return ui_statement_expr(c,s,literal,NULL);
}
static int ui_statement(Cur* c,const unsigned char* s) {return ui_statement_text(c,s,NULL);}
static int ui_window(Cur* c,const unsigned char* b,const char** name,char* image,unsigned cap,unsigned material_slot) {
    unsigned ptr=rd_u32(b+152),first=c->mat_n;const char *mat=NULL,*mapped;
    *name=ui_string(c,rd_u32(b));
    if(image && cap)image[0]=0;
    if(!*name || !xstr(c,rd_u32(b+52)))return 0;
    if(is_neg(ptr)) {
        if(ptr==FF_NEG2)world_alloc(c,4,4);
        if(!material(c))return 0;
        if(c->mat_n>first) {
            mat=c->mat_names+first*64u;
            if(material_slot)remember_mat_addr(material_slot,mat);
        }
        resolve_world_images(c,0);
    } else if(ptr>1u && ((ptr-1u)>>28)==4u)mat=mat_addr_name((ptr-1u)&0x0fffffffu);
    if(!image || !cap || !mat)return 1;
    mapped=color_image(c->colors,c->color_n,mat);
    if((!mapped || !*mapped) && c->ui_localization && c->ui_localization->materials) {
        UiLocalization* lib=c->ui_localization->materials;unsigned i;
        const char* canonical=*mat==','?mat+1:mat;
        for(i=lib->count;i>0;i--)if(!strcmp(lib->entries[i-1].key,canonical)){mapped=lib->entries[i-1].value;break;}
    }
    if(mapped && *mapped && *mapped!='$')snprintf(image,cap,"images/%s",mapped);
    return 1;
}
static int ui_quote(Buf* out,const char* s) {
    if(!buf_str(out,"\""))return 0;
    for(;*s;s++) {
        char ch=*s;
        if(ch=='"' || ch=='\\') { if(!buf_str(out,"\\"))return 0; }
        if(ch=='\n' || ch=='\r' || ch=='\t')ch=' ';
        if(!buf_add(out,&ch,1))return 0;
    }
    return buf_str(out,"\"");
}
/* Compiled action strings use quoted command names and semicolon separators.
 * Unknown commands are skipped as whole statements, including nested blocks. */
static int ui_action_token(const char** p,const char* end,char* out,unsigned cap) {
    unsigned n=0;int quoted;
    while(*p<end && (unsigned char)**p<=32)(*p)++;
    if(*p==end)return 0;
    quoted=**p=='"';if(quoted)(*p)++;
    while(*p<end) {
        char ch=*(*p)++;
        if(quoted && ch=='"'){out[n]=0;return 1;}
        if(!quoted && (unsigned char)ch<=32){out[n]=0;return 1;}
        if(quoted && ch=='\\' && *p<end)ch=*(*p)++;
        if(n+1>=cap)return -1;
        out[n++]=ch;
    }
    out[n]=0;return quoted?-1:1;
}
static int ui_emit_actions(Buf* out,const char* key,const char* script,const char* menu) {
    const char* p=script;int deferred_profile=0;
    if(!script || !*script)return 1;
    if(!buf_str(out," ") || !buf_str(out,key) || !buf_str(out," {"))return 0;
    while(*p) {
        const char *start=p,*end,*token;
        unsigned depth=0,count=0;int quoted=0,bad=0;
        char args[5][1024],discard[1024];const char* cmd=NULL;
        for(;*p;p++) {
            if(quoted && *p=='\\' && p[1]){p++;continue;}
            if(*p=='"'){quoted=!quoted;continue;}
            if(quoted)continue;
            if(*p=='{')depth++;
            else if(*p=='}'){if(!depth)return 0;depth--;}
            else if(*p==';' && !depth)break;
        }
        if(quoted || depth)return 0;
        end=p;if(*p)p++;token=start;
        while(token<end) {
            int status=ui_action_token(&token,end,count<5?args[count]:discard,1024);
            if(status<0){bad=1;break;}if(!status)break;count++;
        }
        if(bad || !count)continue;
        if(count==2 && !strcmp(args[0],"uiScript")) {
            const char* host=!strcmp(args[1],"quit")?"quit":
                !strcmp(args[1],"addPlayerProfiles")?"cod4_profiles_refresh":
                !strcmp(args[1],"createPlayerProfile")?"cod4_profile_create":
                !strcmp(args[1],"loadPlayerProfile")?"cod4_profile_load":
                !strcmp(args[1],"deletePlayerProfile")?"cod4_profile_delete":
                !strcmp(args[1],"selectActivePlayerProfile")?"setfromdvar ui_playerProfileSelected com_playerProfile":NULL;
            if(host){if(!buf_str(out," exec ") || !ui_quote(out,host) || !buf_str(out," ;"))return 0;deferred_profile=1;continue;}
        }
        if(deferred_profile && count==2 && (!strcmp(args[0],"close") || !strcmp(args[0],"open"))) {
            Buf command={0};int ok=buf_str(&command,args[0]) && buf_str(&command," ") &&
                ui_quote(&command,!strcmp(args[1],"self")?menu:args[1]);
            if(ok)ok=buf_str(out," exec ") && ui_quote(out,command.p) && buf_str(out," ;");
            free(command.p);if(!ok)return 0;continue;
        }
        if(count==5 && !strcmp(args[0],"uiScript") &&
           (!strcmp(args[1],"openMenuOnDvar") || !strcmp(args[1],"openMenuOnDvarNot") ||
            !strcmp(args[1],"closeMenuOnDvar") || !strcmp(args[1],"closeMenuOnDvarNot"))) {
            Buf command={0},open={0};int ok;
            ok=buf_str(&open,!strncmp(args[1],"close",5)?"close ":"open ") && ui_quote(&open,args[4]) &&
               buf_str(&command,strstr(args[1],"Not")?"exec_if_string_not ":"exec_if_string ") &&
               ui_quote(&command,args[2]) && buf_str(&command," ") && ui_quote(&command,args[3]) &&
               buf_str(&command," ") && ui_quote(&command,open.p);
            if(ok)ok=buf_str(out," exec ") && ui_quote(out,command.p) && buf_str(out," ;");
            free(open.p);free(command.p);if(!ok)return 0;continue;
        }
        if(count==2 && (!strcmp(args[0],"open") || !strcmp(args[0],"close") ||
           !strcmp(args[0],"play") || !strcmp(args[0],"exec") ||
           !strcmp(args[0],"show") || !strcmp(args[0],"hide")))cmd=args[0];
        else if(count==3 && (!strcmp(args[0],"setdvar") || !strcmp(args[0],"setcvar")))cmd="set";
        else if(count==3 && (!strcmp(args[0],"setLocalVarInt") || !strcmp(args[0],"setLocalVarBool") ||
            !strcmp(args[0],"setLocalVarFloat") || !strcmp(args[0],"setLocalVarString")))cmd="setlocal";
        if(count==4) {
            const char* condition=NULL;Buf command={0};int ok;
            char lower[1024];unsigned j;
            snprintf(lower,sizeof(lower),"%s",args[0]);
            for(j=0;lower[j];j++)if(lower[j]>='A' && lower[j]<='Z')lower[j]=(char)(lower[j]-'A'+'a');
            if(!strcmp(lower,"execondvarintvalue"))condition="exec_if_int ";
            else if(!strcmp(lower,"execondvarfloatvalue"))condition="exec_if_float ";
            else if(!strcmp(lower,"execondvarstringvalue"))condition="exec_if_string ";
            if(!condition)continue;
            ok=buf_str(&command,condition) && ui_quote(&command,args[1]) && buf_str(&command," ") &&
               ui_quote(&command,args[2]) && buf_str(&command," ") && ui_quote(&command,args[3]);
            if(ok)ok=buf_str(out," exec ") && ui_quote(out,command.p) && buf_str(out," ;");
            free(command.p);if(!ok)return 0;continue;
        }
        if(!cmd)continue;
        if(!buf_str(out," ") || !buf_str(out,cmd) || !buf_str(out," ") ||
           !ui_quote(out,!strcmp(cmd,"close") && !strcmp(args[1],"self")?menu:args[1]))return 0;
        if(count==3 && (!buf_str(out," ") || !ui_quote(out,args[2])))return 0;
        if(!buf_str(out," ;"))return 0;
    }
    return buf_str(out," }");
}
static unsigned ui_widget_kind(unsigned type) {
    if(type==6 || type==12 || type==13)return type;
    return type==0 || type==4 || type==9 || type==10 || type==11 || type==14 || type==16 || type==17 || type==18?1:0;
}
static void ui_widgets_free(Cur* c) {
    if(c->ui_widgets){ui_loc_free(c->ui_widgets);free(c->ui_widgets);c->ui_widgets=NULL;}
}
/* Reused widget definitions are VIRTUAL allocations, independent of the item.
   Cache translated properties so shared definitions never reread inline strings. */
static int ui_widget(Cur* c,Buf* out,unsigned type,unsigned ptr,int* known) {
    unsigned kind=ui_widget_kind(type),event,offset,i;const unsigned char* b;
    char key[48],line[160];Buf props={0};int ok=0;
    *known=0;if(!ptr)return 1;
    if(!kind)return !is_neg(ptr);
    if(kind==13) {
        const char* value=ui_string(c,ptr);
        if(!value)return 0;
        if(!strcmp(value,"r_mode")){*known=1;return buf_str(out," feeder \"cod4_video_modes\"");}
        return 1;
    }
    if(!is_neg(ptr)) {
        if(ptr<=1u || ((ptr-1u)>>28)!=4u || !c->world_valid || !c->ui_widgets)return 1;
        snprintf(key,sizeof(key),"%u:%x",kind,(ptr-1u)&0x0fffffffu);
        for(i=0;i<c->ui_widgets->count;i++)if(!strcmp(c->ui_widgets->entries[i].key,key)) {
            *known=strstr(c->ui_widgets->entries[i].value," numericRange 1")?2:1;return buf_str(out,c->ui_widgets->entries[i].value);
        }
        return 1;
    }
    event=world_alloc(c,4,kind==6?340:kind==12?392:32);
    offset=event?c->world_allocs[event-1u].offset:0;
    b=take(c,kind==6?340:kind==12?392:32);if(!b)return 0;
    if(kind==12) {
        const char* labels[32];const char* values[32];unsigned count=rd_u32(b+384),strings=rd_u32(b+388);
        int resolved=1;
        if(count>32 || strings>1)goto done;
        for(i=0;i<32;i++) {
            unsigned p=rd_u32(b+i*4u);labels[i]=ui_string(c,p);
            if(!labels[i] && is_neg(p))goto done;
            if(i<count && !labels[i])resolved=0;
        }
        for(i=0;i<32;i++) {
            unsigned p=rd_u32(b+128+i*4u);values[i]=ui_string(c,p);
            if(!values[i] && is_neg(p))goto done;
            if(i<count && strings && !values[i])resolved=0;
        }
        if(!resolved){ok=1;goto done;}
        if(!buf_str(&props," choices {"))goto done;
        for(i=0;i<count;i++) {
            const char* value=values[i];
            if(!strings){float n=rd_f32(b+256+i*4u);if(!finite_f(n))goto done;snprintf(line,sizeof(line),"%.9g",n);value=line;}
            if(!buf_str(&props," ") || !ui_quote(&props,value) || !buf_str(&props," ") ||
               !ui_quote(&props,ui_loc_text(c->ui_localization,labels[i])))goto done;
        }
        if(!buf_str(&props," }"))goto done;
    } else if(kind==6) {
        if(!xstr(c,rd_u32(b+288)))goto done;
        if(is_neg(rd_u32(b+336)) && !material(c))goto done;
        if(finite_f(rd_f32(b+20)) && rd_f32(b+20)>=1 && rd_f32(b+20)<=480) {
            snprintf(line,sizeof(line)," rowHeight %.9g",rd_f32(b+20));if(!buf_str(&props,line))goto done;
        }
    } else {
        float min=rd_f32(b),max=rd_f32(b+4);unsigned chars=rd_u32(b+16);
        if(finite_f(min) && finite_f(max) && max>min) {
            snprintf(line,sizeof(line)," min %.9g max %.9g step 0 numericRange 1",min,max);if(!buf_str(&props,line))goto done;
        }
        if(chars>0 && chars<=4096){snprintf(line,sizeof(line)," maxChars %u",chars);if(!buf_str(&props,line))goto done;}
    }
    if(!props.p && !buf_str(&props," "))goto done;
    if(!c->ui_widgets){c->ui_widgets=(UiLocalization*)calloc(1,sizeof(UiLocalization));if(!c->ui_widgets)goto done;}
    snprintf(key,sizeof(key),"%u:%x",kind,offset);
    if(!ui_loc_store(c->ui_widgets,key,props.p) || !buf_add(out,props.p,props.n))goto done;
    *known=strstr(props.p," numericRange 1")?2:1;ok=1;
done:
    free(props.p);return ok;
}
/* Resolve screen-edge/center anchors into Helix's canonical 640x480 frame.
   Native pixel and inverse-scale modes still require host viewport support. */
static float ui_rect_coord(float position,unsigned align,float extent) {
    if(align==2 || align==7)return position+extent*0.5f;
    if(align==3)return position+extent;
    return position;
}
static int ui_item_relative(Cur* c,Buf* out,unsigned index,const char* menu,float parent_x,float parent_y) {
    const unsigned char* b;
    world_alloc(c,4,372);b=take(c,372);
    const char *name,*text,*action=NULL,*on_focus=NULL,*leave_focus=NULL,*dvar="";
    int widget_known=0;const char* native_type;
    unsigned i,type;
    char line[512],image[128];
    if(!b || !ui_window(c,b,&name,image,sizeof(image),c->world_alloc_n?c->world_allocs[c->world_alloc_n-1u].offset+152u:0))return 0;
    text=ui_string(c,rd_u32(b+224));if(!text)return 0;
    for(i=236;i<=272;i+=4) {
        const char* s=ui_string(c,rd_u32(b+i));if(!s)return 0;
        if(i==252)action=s;
        if(i==260)on_focus=s;
        if(i==264)leave_focus=s;
        if(i==268)dvar=s;
    }
    if(!ui_keys(c,rd_u32(b+276),0) || !xstr(c,rd_u32(b+280)))return 0;
    if(is_neg(rd_u32(b+288)) && !sound_alias(c))return 0;
    type=rd_u32(b+180);
    if(!buf_str(out," itemDef {"))return 0;
    /* IW3 windowDef_t::staticFlags, WINDOW_FLAG_AUTO_WRAPPED. */
    if((rd_u32(b+76)&0x800000u) && !buf_str(out," textWrap 1"))return 0;
    /* Scroll the complete stock row, including its highlight/background layers.
       Tabs, headings, Back and Apply stay outside the content viewport. */
    if(!strcmp(menu,"options_graphics") && ui_rect_coord(rd_f32(b+28),rd_u32(b+44),640)>=246.f &&
       ui_rect_coord(rd_f32(b+32),rd_u32(b+48),480)>=98.f &&
       ui_rect_coord(rd_f32(b+32),rd_u32(b+48),480)<=406.f && rd_f32(b+40)<=22.f &&
       !buf_str(out," scrollable 1"))return 0;
    if(!ui_widget(c,out,type,rd_u32(b+300),&widget_known))return 0;
    /* Emit visibility IR before the remaining item properties; the Helix parser
       accepts item fields in any order. Shared entries keep their decoded strings. */
    for(i=308;i<372;i+=8) {
        const char* literal=NULL;Buf expression={0};int ok;
        ok=ui_statement_expr(c,b+i,i==316?&literal:NULL,(i==308 || i>=332)?&expression:NULL);
        if(ok && expression.n) {
            const char* key=i==308?" iw3Visible {":i==332?" iw3RectX {":i==340?" iw3RectY {":i==348?" iw3RectW {":i==356?" iw3RectH {":" iw3Alpha {";
            ok=buf_str(out,key) && buf_add(out,expression.p,expression.n) && buf_str(out," }");
        }
        free(expression.p);if(!ok)return 0;
        if(literal)text=literal;
    }
    /* Native action equivalents are translated here; IW3-specific commands
       and dynamic widget semantics still require the runtime bridge. */
    if(!buf_str(out," name "))return 0;
    if(*name){if(!ui_quote(out,name))return 0;}
    else {snprintf(line,sizeof(line),"\"item_%u\"",index);if(!buf_str(out,line))return 0;}
    native_type=(action && *action)?"button":*image?"image":"label";
    if(type==6 && !strcmp(name,"profilelist")) {
        native_type="listbox";dvar="ui_playerProfileSelected";
        if(!buf_str(out," feeder \"cod4_profiles\""))return 0;
    }
    if(*dvar) {
        if(type==2 || type==3)native_type="checkbox";
        else if(type==11)native_type="multi";
        else if((type==12 || type==13) && widget_known)native_type="multi";
        else if(type==10 && widget_known==2)native_type="slider";
        else if(type==4 || type==9 || type==16 || type==17 || type==18)native_type="editfield";
    }
    snprintf(line,sizeof(line)," type %s rect %.5g %.5g %.5g %.5g text ",
             native_type,ui_rect_coord(rd_f32(b+28),rd_u32(b+44),640)-(ui_rect_coord(0,rd_u32(b+44),640)!=0?parent_x:0),ui_rect_coord(rd_f32(b+32),rd_u32(b+48),480)-(ui_rect_coord(0,rd_u32(b+48),480)!=0?parent_y:0),rd_f32(b+36),rd_f32(b+40));
    if(!buf_str(out,line) || !ui_quote(out,ui_loc_text(c->ui_localization,text)))return 0;
    if(*dvar && (!buf_str(out," cvar ") || !ui_quote(out,!strcmp(dvar,"snd_volume")?"s_volume":dvar)))return 0;
    if(type==11 && !buf_str(out," choices { \"0\" \"No\" \"1\" \"Yes\" }"))return 0;
    if((type==9 || type==17) && !buf_str(out," numeric 1"))return 0;
    if(*image && (!buf_str(out," image ") || !ui_quote(out,image)))return 0;
    snprintf(line,sizeof(line)," iw3TextAlign %u textalign %s textalignx %.5g textaligny %.5g",rd_u32(b+196),(rd_u32(b+196)&3u)==1?"center":(rd_u32(b+196)&3u)==2?"right":"left",rd_f32(b+200),rd_f32(b+204));
    if(!buf_str(out,line))return 0;
    snprintf(line,sizeof(line)," textscale %.5g forecolor %.5g %.5g %.5g %.5g ",rd_f32(b+208),rd_f32(b+88),rd_f32(b+92),rd_f32(b+96),rd_f32(b+100));
    return buf_str(out,line) && ui_emit_actions(out,"action",action,menu) &&
        ui_emit_actions(out,"onFocus",on_focus,menu) && ui_emit_actions(out,"onLeave",leave_focus,menu) && buf_str(out," }\n");
}
static int ui_item(Cur* c,Buf* out,unsigned index,const char* menu) { return ui_item_relative(c,out,index,menu,0,0); }
static int ui_helix_graphics(Buf* out) {
    static const struct {const char* key;const char* label;const char* choices;} rows[]={
#ifdef _WIN32
        {"r_renderer","Renderer","\"vulkan\" \"Vulkan\" \"opengl\" \"OpenGL\" \"opengl33\" \"OpenGL 3.3\" \"d3d9\" \"Direct3D 9\" \"d3d11\" \"Direct3D 11\" \"d3d12\" \"Direct3D 12\" \"software\" \"Software\""},
#else
        {"r_renderer","Renderer","\"vulkan\" \"Vulkan\" \"opengl\" \"OpenGL\" \"opengl33\" \"OpenGL 3.3\" \"software\" \"Software\""},
#endif
        {"r_borderless","Borderless Window",NULL},
        {"r_anisotropy","Anisotropic Filtering","\"0\" \"Off\" \"2\" \"2x\" \"4\" \"4x\" \"8\" \"8x\" \"16\" \"16x\""},
        {"r_pbr","Dynamic PBR Lighting",NULL},
        {"r_aces","ACES Tonemapping",NULL},
        {"r_physical_falloff","Physical Light Falloff",NULL},
        {"r_shadowAtlas","Shadow Atlas",NULL},
        {"r_surfaceDetail","Surface Relief",NULL},
        {"r_bumpScale","Normal Mapping",NULL}
    };
    unsigned i;char line[768];
    for(i=0;i<sizeof(rows)/sizeof(rows[0]);i++) {
        snprintf(line,sizeof(line)," itemDef { name hx_%s type multi rect 246 %u 300 20 text \"%s\" cvar \"%s\" scrollable 1 iw3TextAlign 8 textalign left textalignx 0 textscale .33 forecolor .69 .69 .69 1 choices { %s } action { setlocal \"ui_showApply\" \"1\" ; } }\n",
            rows[i].key,428+i*22,rows[i].label,rows[i].key,rows[i].choices?rows[i].choices:"\"0\" \"Off\" \"1\" \"On\"");
        if(!buf_str(out,line))return 0;
    }
    return buf_str(out," scrollRegion 98 404 626\n");
}
static int ui_menu(Cur* c,Buf* out) {
    const unsigned char* b=take(c,284),*items;
    const char* name;
    const char* scripts[3];
    unsigned n,i;
    char line[256];
    if(!b || !ui_window(c,b,&name,NULL,0,0) || !xstr(c,rd_u32(b+156)))return 0;
    for(i=0;i<3;i++){scripts[i]=ui_string(c,rd_u32(b+196+i*4));if(!scripts[i])return 0;}
    if(!ui_keys(c,rd_u32(b+208),0) || !ui_statement(c,b+212) ||
       !xstr(c,rd_u32(b+220)) || !xstr(c,rd_u32(b+224)) ||
       !ui_statement(c,b+264) || !ui_statement(c,b+272))return 0;
    if(!buf_str(out,"menuDef { name ") || !ui_quote(out,name))return 0;
    snprintf(line,sizeof(line)," rect %.5g %.5g %.5g %.5g fullscreen %u codstyle 1 visible 0\n",ui_rect_coord(rd_f32(b+4),rd_u32(b+20),640),ui_rect_coord(rd_f32(b+8),rd_u32(b+24),480),rd_f32(b+12),rd_f32(b+16),rd_u32(b+160));
    if(!buf_str(out,line))return 0;
    if(!ui_emit_actions(out,"onOpen",scripts[0],name) || !ui_emit_actions(out,"onClose",scripts[1],name) ||
       !ui_emit_actions(out,"onESC",scripts[2],name) || !buf_str(out,"\n"))return 0;
    n=rd_u32(b+164);if(n>4096)return 0;
    if(is_neg(rd_u32(b+280))) {
        world_alloc(c,4,n*4u);
        items=take(c,n*4u);if(!items)return 0;
        for(i=0;i<n;i++)if(is_neg(rd_u32(items+i*4u)) && !ui_item_relative(c,out,i,name,ui_rect_coord(0,rd_u32(b+20),640),ui_rect_coord(0,rd_u32(b+24),480)))return 0;
    }
    if(!strcmp(name,"options_graphics") && !ui_helix_graphics(out))return 0;
    return buf_str(out,"}\n");
}
static int ui_list(Cur* c,Buf* out) {
    const unsigned char* b=take(c,12),*menus;
    unsigned n,i;
    if(!b || !xstr(c,rd_u32(b)))return 0;
    n=rd_u32(b+4);if(n>4096)return 0;
    if(!is_neg(rd_u32(b+8)))return 1;
    world_alloc(c,4,n*4u);
    menus=take(c,n*4u);if(!menus)return 0;
    for(i=0;i<n;i++)if(is_neg(rd_u32(menus+i*4u)) && !ui_menu(c,out))return 0;
    return 1;
}

/* Font children must be walked before localized entries in code_post_gfx. */
static int ui_font(Cur* c) {
    const unsigned char* b=take(c,24);unsigned n;
    if(!b || !xstr(c,rd_u32(b)))return 0;
    n=rd_u32(b+8);if(n>65536)return 0;
    /* Inserted TEMP material pointers still reserve a VIRTUAL pointer slot. */
    if(rd_u32(b+12)==FF_NEG2)world_alloc(c,4,4);
    if(is_neg(rd_u32(b+12)) && !material(c))return 0;
    if(rd_u32(b+16)==FF_NEG2)world_alloc(c,4,4);
    if(is_neg(rd_u32(b+16)) && !material(c))return 0;
    if(is_neg(rd_u32(b+20))) {
        world_alloc(c,4,n*24u);if(!take(c,n*24u))return 0;
    }
    return 1;
}
static int ui_localize(Cur* c,Buf* out) {
    const unsigned char* b=take(c,8);const char *value,*name;
    if(!b)return 0;
    value=ui_string(c,rd_u32(b));name=ui_string(c,rd_u32(b+4));
    if(!value || !name || !*name)return 0;
    if(c->ui_localization && !ui_loc_store(c->ui_localization,name,value))return 0;
    return ui_quote(out,name) && buf_str(out," ") && ui_quote(out,value) && buf_str(out,"\n");
}

/* Additional typed children used by the common UI asset zones. */
static int ui_string_table_export(Cur* c,Buf* out) {
    const unsigned char* b=take(c,16),*values;unsigned columns,rows,n,i;const char* name;char line[80];
    if(!b)return 0;
    if(out){name=ui_string(c,rd_u32(b));if(!name)return 0;}
    else {name="";if(!xstr(c,rd_u32(b)))return 0;}
    columns=rd_u32(b+4);rows=rd_u32(b+8);
    if(columns>4096 || rows>100000 || (columns && rows>1000000u/columns))return 0;
    n=columns*rows;if(!is_neg(rd_u32(b+12)))return 1;
    world_alloc(c,4,n*4u);values=take(c,n*4u);if(!values)return 0;
    if(!out){for(i=0;i<n;i++)if(!xstr(c,rd_u32(values+i*4u)))return 0;return 1;}
    if(out){snprintf(line,sizeof(line)," %u %u {\n",columns,rows);if(!buf_str(out,"stringTable ") || !ui_quote(out,name) || !buf_str(out,line))return 0;}
    for(i=0;i<n;i++){
        const char* cell;int mode=c->ui_assets;if(out)c->ui_assets=1;
        cell=ui_string(c,rd_u32(values+i*4u));c->ui_assets=mode;if(!cell){fprintf(stderr,"CoD4 StringTable %s: unresolved cell %u pointer %08x (valid=%d)\n",name,i,rd_u32(values+i*4u),c->world_valid);return 0;}
        if(out && (!ui_quote(out,cell) || !buf_str(out,(i+1)%columns?" ":"\n")))return 0;
    }
    return !out || buf_str(out,"}\n");
}
static int ui_string_table(Cur* c) {return ui_string_table_export(c,NULL);}
static int ui_named_asset(Cur* c,unsigned size,unsigned extra_string) {
    const unsigned char* b=take(c,size);
    return b && xstr(c,rd_u32(b)) && (!extra_string || xstr(c,rd_u32(b+extra_string)));
}
static int ui_impact_table(Cur* c) {
    const unsigned char* b=take(c,8),*entries;unsigned i;
    /* Serialized table has 12 entries, each with 29 surfaces + 4 flesh variants.
       See OpenAssetTools src/ZoneCode/Game/IW3/XAssets/FxImpactTable.txt. */
    if(!b || !xstr(c,rd_u32(b)))return 0;
    if(!is_neg(rd_u32(b+4)))return 1;
    entries=take(c,12u*33u*4u);if(!entries)return 0;
    for(i=0;i<12u*33u;i++)if(is_neg(rd_u32(entries+i*4u)) && !fx_effect(c))return 0;
    return 1;
}

/* Original layout import with optional localization and native action mapping.
   Dynamic expressions, material bindings and IW3 UIScript remain incomplete. */
static int ui_export_zone(const unsigned char* data,unsigned size,unsigned char** out_buf,unsigned* out_size,int localization,UiLocalization* loc) {
    unsigned char* zone=NULL;
    unsigned zn=0,ns,na,i,asset_base;
    const unsigned char *strings,*assets;
    Cur c={0};Buf out={0},discard={0};int ok=0;
    if(!out_buf || !out_size)return 0;
    *out_buf=NULL;*out_size=0;
    if(!data || size<16 || memcmp(data,"IWffu100",8) || rd_u32(data+8)!=5)return 0;
    if(!inflate_zlib(data+12,size-12,&zone,&zn) || zn<60)goto done;
    c.ui_assets=localization>=2?2:1;c.ui_localization=loc;
    c.d=zone;c.n=zn;c.o=60;c.world_tracking=c.world_valid=c.virtual_absolute=1;
    ns=rd_u32(zone+44);na=rd_u32(zone+52);
    if(ns>100000 || !na || na>100000 || !is_neg(rd_u32(zone+56)))goto done;
    if(is_neg(rd_u32(zone+48))) {
        world_alloc(&c,4,ns*4u);
        strings=take(&c,ns*4u);if(!strings)goto done;
        for(i=0;i<ns;i++)if(!xstr(&c,rd_u32(strings+i*4u)))goto done;
    }
    if(!world_alloc(&c,4,na*8u))goto done;
    asset_base=c.world_allocs[c.world_alloc_n-1u].offset;
    assets=take(&c,na*8u);if(!assets)goto done;
    g_inl_n=0;g_mat_addr_n=0;
    for(i=0;i<na;i++) {
        unsigned t=rd_u32(assets+i*8u),ptr=rd_u32(assets+i*8u+4u);
        if(!is_neg(ptr))continue;
        if(t==5) { if(!techset(&c))goto done; }
        else if(t==4) {
            unsigned first=c.mat_n;
            if(!material(&c))goto done;
            if(c.mat_n>first)remember_mat_addr(asset_base+i*8u+4u,c.mat_names+first*64u);
        }
        else if(t==20 && localization!=1) { resolve_world_images(&c,0);if(!ui_list(&c,localization==3?&discard:&out))goto done; }
        else if(t==22 && (localization==1 || localization==3)) { if(!ui_localize(&c,localization==3?&discard:&out))goto done; }
        else if(t==19) { if(!ui_font(&c))goto done; }
        else if(t==7) { if(!sound_alias(&c))goto done; }
        else if(t==6) { if(!image_ptr(&c,ptr))goto done; }
        else if(t==32) { if(!ui_string_table_export(&c,localization==3?&out:NULL))goto done; }
        else if(t==8) { if(!ui_named_asset(&c,72,0))goto done; }
        else if(t==24) { if(!ui_named_asset(&c,4,0))goto done; }
        else if(t==1) { if(!ui_named_asset(&c,44,28))goto done; }
        else if(t==2) { if(!anim_parts(&c))goto done; }
        else if(t==3) { if(!xmodel(&c))goto done; }
        else if(t==17) { if(!light_def(&c))goto done; }
        else if(t==25) { if(!fx_effect(&c))goto done; }
        else if(t==26) { if(!ui_impact_table(&c))goto done; }
        else if(t==23) { if(!weapon_def(&c))goto done; }
        else if(t==31) { if(!raw_file(&c))goto done; }
        else goto done;
    }
    if(localization==2) {
        if(c.o!=c.n)goto done;
        out.n=0;if(out.p)out.p[0]=0;
        for(i=0;i<c.color_n;i++)if(c.colors[i].mat[0] && c.colors[i].mat[0]!=',' && c.colors[i].image[0]) {
            if(!ui_quote(&out,c.colors[i].mat) || !buf_str(&out," ") || !ui_quote(&out,c.colors[i].image) || !buf_str(&out,"\n"))goto done;
            if(loc && loc->materials && !ui_loc_store(loc->materials,c.colors[i].mat,c.colors[i].image))goto done;
        }
    }
    if(localization==3 && c.o!=c.n)goto done;
    if(!out.n)goto done;
    *out_buf=(unsigned char*)out.p;*out_size=out.n;out.p=NULL;ok=1;
done:
    ui_widgets_free(&c);ui_expr_free(&c);
    for(i=0;i<c.prop_n;i++){free(c.props[i].surfs);free(c.props[i].coll);}free(c.props);
    free(discard.p);free(out.p);free(c.world_allocs);free(c.colors);free(c.mat_names);free(zone);
    return ok;
}

int cod4_ff_to_menu_layout(const unsigned char* data,unsigned size,unsigned char** out_buf,unsigned* out_size) {
    return ui_export_zone(data,size,out_buf,out_size,0,NULL);
}
int cod4_ff_to_localization(const unsigned char* data,unsigned size,unsigned char** out_buf,unsigned* out_size) {
    return ui_export_zone(data,size,out_buf,out_size,1,NULL);
}

int cod4_ff_to_localized_menu_layout(const unsigned char* data,unsigned size,
    const unsigned char* localized_data,unsigned localized_size,unsigned char** out_buf,unsigned* out_size) {
    return cod4_ff_to_menu_bundle(data,size,localized_data,localized_size,NULL,0,out_buf,out_size);
}
int cod4_ff_to_menu_bundle(const unsigned char* data,unsigned size,
    const unsigned char* localized_data,unsigned localized_size,
    const Cod4UiZone* shared_zones,unsigned shared_count,unsigned char** out_buf,unsigned* out_size) {
    UiLocalization loc={0},materials={0};unsigned char* scratch=NULL;unsigned scratch_size=0,i;int ok=0;
    if(!out_buf || !out_size)return 0;
    *out_buf=NULL;*out_size=0;
    if(shared_count>32 || (shared_count && !shared_zones))return 0;
    loc.materials=&materials;
    for(i=0;i<shared_count;i++) {
        if(!ui_export_zone(shared_zones[i].data,shared_zones[i].size,&scratch,&scratch_size,2,&loc))goto done;
        free(scratch);scratch=NULL;
    }
    if(!ui_export_zone(localized_data,localized_size,&scratch,&scratch_size,1,&loc))goto done;
    free(scratch);scratch=NULL;
    ok=ui_export_zone(data,size,out_buf,out_size,0,&loc);
done:
    free(scratch);ui_loc_free(&loc);ui_loc_free(&materials);return ok;
}

int cod4_ff_to_ui_materials(const unsigned char* data,unsigned size,unsigned char** out_buf,unsigned* out_size) {
    return ui_export_zone(data,size,out_buf,out_size,2,NULL);
}

int cod4_ff_to_string_tables(const unsigned char* data,unsigned size,unsigned char** out_buf,unsigned* out_size) {return ui_export_zone(data,size,out_buf,out_size,3,NULL);}
