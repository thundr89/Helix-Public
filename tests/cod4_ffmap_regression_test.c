#include "../src/cod4/cod4_ffmap.c"
#undef NDEBUG
#include <assert.h>
static void u32(unsigned char* p,unsigned v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void f32(unsigned char* p,float f){unsigned u;memcpy(&u,&f,4);u32(p,u);}
static void test_lod(void){
 unsigned char data[220]={0}; Cur c={0};
 data[6]=6; data[44]=2; data[46]=1; c.d=data;c.n=sizeof(data);
 assert(xmodel(&c)); assert(c.props[0].surf_n==6);
 assert(c.props[0].lod_first==1 && c.props[0].lod_count==2);
 free(c.props[0].surfs);free(c.props);
}
static void test_alpha(unsigned bits,int expected){
 unsigned char data[90]={0}; Cur c={0}; ColorMap row={0};
 u32(data,FF_NEG1);data[60]=1;u32(data+76,FF_NEG1);
 data[80]='x';u32(data+82,bits);strcpy(row.mat,"x");strcpy(row.image,"test");
 c.d=data;c.n=sizeof(data);c.colors=&row;c.color_n=1;c.color_cap=1;
 assert(material(&c));assert(row.blend==expected);if(bits & 0x3000)assert(row.cutout==3);free(c.mat_names);
}
static void test_ladder(void){
 unsigned char br[80]={0},mat[72]={0};WorldGeom w={0};Buf out={0};
 strcpy((char*)mat,"ladder");u32(br+12,0x10000);
 f32(br+16,16);f32(br+20,16);f32(br+24,128);
 w.brushes=br;w.brush_n=1;w.cmats=mat;w.cmat_n=1;
 assert(emit_clip_brushes(&out,&w,1));assert(out.p && strstr(out.p,"contents ladder"));
 free(out.p);
}
static void test_instance_export(void) {
    unsigned char draw[76] = {0}, verts[96] = {0}, indices[6] = {0};
    PropSurf surfs[2] = {{0}};
    PropModel model = {0};
    float coll[9]={0,0,5,0,2,5,2,0,5};
    WorldGeom w = {0}; Buf out = {0};
    unsigned i;
    float x,y,z,u,v,nx,ny,nz;
    u32(draw + 56, 0x40000005u);
    /* Rotate +X onto +Y. */
    f32(draw + 20, 1); f32(draw + 28, -1); f32(draw + 48, 1); f32(draw + 52, 1);
    for (i=0;i<3;i++) {
        verts[i*32+16]=64;verts[i*32+17]=128;verts[i*32+18]=192;verts[i*32+19]=32;
        verts[i*32+20]=0;verts[i*32+21]=0x38; /* V=.5 */
        verts[i*32+22]=0;verts[i*32+23]=0x34; /* U=.25 */
        verts[i*32+24]=254; verts[i*32+25]=127; verts[i*32+26]=127;
    }
    indices[2]=1; indices[4]=2;
    for(i=0;i<2;i++) {
        surfs[i].vert=verts; surfs[i].vc=3; surfs[i].tri=indices; surfs[i].tc=1;
        strcpy(surfs[i].image,i ? "images/low" : "images/high");
    }
    model.surfs=surfs; model.surf_n=2; model.lod_count=1;
    model.coll=coll;model.coll_n=1;
    w.asset_slot=4; w.props=&model; w.prop_n=1; w.draws=draw; w.draw_n=1;
    assert(emit_static_models(&out,&w));
    assert(strstr(out.p,"images/high") && !strstr(out.p,"images/low"));
    assert(strstr(out.p,"  f 0 2 1\n"));
    assert(sscanf(strstr(out.p,"  v ")+4,"%f %f %f %f %f %f %f %f",&x,&y,&z,&u,&v,&nx,&ny,&nz)==8);
    assert(fabsf(u-.25f)<.0001f && fabsf(v-.5f)<.0001f);
    {
        float lu,lv,r,g,b,a;
        assert(sscanf(strstr(out.p,"  v ")+4,"%f %f %f %f %f %f %f %f %f %f %f %f %f %f",&x,&y,&z,&u,&v,&nx,&ny,&nz,&lu,&lv,&r,&g,&b,&a)==14);
        assert(fabsf(r-64.f/255)<.0001f && fabsf(g-128.f/255)<.0001f && fabsf(b-192.f/255)<.0001f && fabsf(a-32.f/255)<.0001f);
    }
    assert(fabsf(nx)<0.001f && fabsf(ny-1)<0.001f && fabsf(nz)<0.001f);
    assert(sscanf(strstr(strstr(out.p,"visible 0"),"  v ")+4,"%f %f %f %f %f %f %f %f",&x,&y,&z,&u,&v,&nx,&ny,&nz)==8);
    assert(fabsf(nx)<.001f && fabsf(ny)<.001f && fabsf(nz+1)<.001f);
    free(out.p);
}
static void test_authored_decals(void) {
    Cur c = {0};
    assert(mat_usable("wc/killhouse_marks"));
    assert(mat_usable("wc/icbm_decal_ak47poster"));
    assert(mat_usable("wc/decal_porterhaus"));
    assert(mat_usable("wc/me_decal_adobe_top_01"));
    assert(mat_usable("wc/fan_map_overlay"));
    assert(remember_color(&c, "wc/fan_decal_marks", "fan_overlay"));
    /* A suggestive filename must not override authored opaque state. */
    assert(c.colors[0].blend == 0);
    free(c.colors);
}
static void collision_record(unsigned char* raw) {
    /* Triangle (10,20,30), (14,20,30), (12,23,30): skew, non-unit coordinate rows. */
    f32(raw+8,1); f32(raw+12,30);
    f32(raw+16,.25f); f32(raw+20,-1.f/6.f); f32(raw+28,10.f/4.f-20.f/6.f);
    f32(raw+36,1.f/3.f); f32(raw+44,20.f/3.f);
}
static void test_collision_coordinates(void) {
    unsigned char raw[48]={0}; float tri[9];
    const float expected[9]={10,20,30,14,20,30,12,23,30}; unsigned k;
    collision_record(raw); assert(collision_triangle(raw,tri));
    for(k=0;k<9;k++)assert(fabsf(tri[k]-expected[k])<.0001f);
    f32(raw+8,-1); f32(raw+12,-30);assert(collision_triangle(raw,tri));
    /* Reversed outward plane requires reversed triangle winding. */
    for(k=0;k<3;k++){assert(fabsf(tri[3+k]-expected[6+k])<.0001f);}
    memset(raw,0,sizeof(raw)); assert(!collision_triangle(raw,tri));
    f32(raw,0.f/0.f);assert(!collision_triangle(raw,tri));
}
static void test_collision_contents(void) {
    unsigned char data[220+88+96]={0}; Cur c={0};
    u32(data+152,FF_NEG1);u32(data+156,2);
    u32(data+220,FF_NEG1);u32(data+224,1);u32(data+220+36,2);
    u32(data+264,FF_NEG1);u32(data+268,1);u32(data+264+36,1);
    collision_record(data+308);collision_record(data+356);
    c.d=data;c.n=sizeof(data);assert(xmodel(&c));
    assert(c.props[0].coll_n==1);assert(fabsf(c.props[0].coll[0]-10)<.0001f);
    free(c.props[0].coll);free(c.props);
}
static void test_visible_pass_state(void) {
    unsigned char data[98]={0}; Cur c={0}; ColorMap row={0};
    u32(data,FF_NEG1);data[60]=2;u32(data+76,FF_NEG1);data[80]='x';
    memset(data+24,255,34);data[24+7]=1;
    u32(data+82,0x800|2|(1<<4)); /* opaque depth pass */
    u32(data+90,0x3000|0x4000|2|(1<<4)); /* two-sided cutout lit pass */
    strcpy(row.mat,"x");c.d=data;c.n=sizeof(data);c.colors=&row;c.color_n=1;c.color_cap=1;
    assert(material(&c));assert(row.cutout==3 && row.cull==1 && row.blend==0 && !row.unlit);
    assert(!row.camera_hidden);
    free(c.mat_names);
    memset(&c,0,sizeof(c));memset(&row,0,sizeof(row));strcpy(row.mat,"x");
    memset(data+24,255,34);data[24+4]=1;u32(data+90,0x800|2|(2<<4));
    c.d=data;c.n=sizeof(data);c.colors=&row;c.color_n=1;c.color_cap=1;
    assert(material(&c));assert(row.blend==2 && row.unlit);free(c.mat_names);
    memset(&c,0,sizeof(c));memset(&row,0,sizeof(row));strcpy(row.mat,"x");
    data[62]=3;c.d=data;c.n=sizeof(data);c.colors=&row;c.color_n=1;c.color_cap=1;
    assert(material(&c));assert(row.camera_hidden);free(c.mat_names);
}
static void test_shared_model_image(void) {
    unsigned char data[165]={0}; Cur c={0}; PropSurf surfs[3]={{0}};
    u32(data,FF_NEG1);data[58]=2;u32(data+68,FF_NEG1);
    memcpy(data+80,"mc/mtl_pine_canopy",19);
    data[99+7]=5;data[111+7]=2;u32(data+111+8,FF_NEG1);
    u32(data+123+32,FF_NEG1);memcpy(data+159,"atlas",6);
    c.d=data;c.n=sizeof(data);c.image_table_prefix=28;c.first_model_material=1;
    assert(material(&c));assert(c.colors[0].image_slot_relative==68);
    assert(remember_color(&c,"mc/another_surface",""));c.colors[1].image_ref=0x40001045u;c.colors[1].cutout=3;
    surfs[0].mat_ptr=FF_NEG1;strcpy(surfs[0].mat_name,"mc/mtl_pine_canopy");
    surfs[1].mat_ptr=0x40001001u;strcpy(surfs[1].mat_name,"mc/mtl_pine_canopy");
    surfs[2].mat_ptr=FF_NEG1;strcpy(surfs[2].mat_name,"mc/another_surface");
    resolve_model_images(&c,surfs,3,0);
    assert(strcmp(c.colors[1].image,"atlas")==0 && c.colors[1].cutout==3);
    assert(strstr(surfs[2].image,"atlas*cutout=0.5"));
    free(c.colors);free(c.mat_names);
}
static void test_shared_image_keeps_state(void) {
    Cur c={0};
    assert(remember_color(&c,"wc/shared","atlas"));
    assert(remember_color(&c,"wc/shared_decal",""));
    c.colors[1].blend=1;
    assert(strcmp(color_image(c.colors,c.color_n,"wc/shared_decal"),"atlas")==0);
    assert(color_row(c.colors,c.color_n,"wc/shared_decal")->blend==1);
    free(c.colors);
}
static void test_world_layers_keep_import_policy(void) {
 ColorMap row={0};char world[256];row.tint[0]=row.tint[1]=row.tint[2]=1;
 row.blend=2;row.cutout=3;row.cull=1;row.unlit=1;
 assert(decorate_image("atlas",&row,world,sizeof(world)));assert(!strcmp(world,"images/atlas"));
 row.world_alpha=1;assert(decorate_image("atlas",&row,world,sizeof(world)));assert(!strcmp(world,"images/atlas*a"));
}
static void test_world_vertex_export(void) {
 unsigned char verts[132]={0},indices[6]={0},surf[48]={0};char names[64]="wc/test";
 WorldGeom w={0};ColorMap color={0};Buf out={0};unsigned i;
 strcpy(color.mat,"wc/test");strcpy(color.image,"test");color.tint[0]=color.tint[1]=color.tint[2]=1;
 for(i=0;i<3;i++){verts[i*44+16]=64;verts[i*44+17]=128;verts[i*44+18]=192;verts[i*44+19]=255;verts[i*44+36]=127;verts[i*44+37]=127;verts[i*44+38]=254;}
 f32(verts+44,2);f32(verts+88+4,2);indices[2]=2;indices[4]=1;
 surf[8]=3;surf[10]=1;u32(surf+16,0x40000015u);
 w.vert=verts;w.vert_n=3;w.idx=indices;w.idx_n=3;w.surf=surf;w.surf_n=1;
 w.world_mat_names=names;w.world_mat_n=1;w.surf_mat_ok=1;w.surf_mat_lo=w.surf_mat_hi=20;
 w.colors=&color;w.color_n=1;
 assert(emit_draw_surfaces(&out,&w));assert(strstr(out.p," f 0 1 2"));
 assert(strstr(out.p," v 0 0 0 0 0"));free(out.p);
}
static void test_surface_asset_slots(void) {
 WorldGeom w={0};unsigned char dummy[1]={0};char names[3*64]={0};
 w.vert=w.idx=w.surf=dummy;w.world_mat_names=names;w.world_mat_n=3;
 w.surf_mat_ok=1;w.surf_mat_lo=20;w.surf_mat_hi=36;
 strcpy(names,"wc/wall");strcpy(names+128,"wc/decal");
 w.mat_n=99;w.mat_at_surfs=99; /* Unrelated later materials cannot shift slots. */
 assert(!strcmp(surf_mat_name(&w,0x40000000u+20u+1u),"wc/wall"));
 assert(!strcmp(surf_mat_name(&w,0x40000000u+36u+1u),"wc/decal"));
 assert(!surf_mat_name(&w,0x40000000u+28u+1u));
 assert(!surf_mat_name(&w,0x40000000u+44u+1u));
}
static void test_playerclip_brush(void) {
 unsigned char brush[80]={0};WorldGeom w={0};Buf out={0};
 u32(brush+12,0x10000);f32(brush+16,9000);f32(brush+20,64);f32(brush+24,64);
 w.brushes=brush;w.brush_n=1;assert(emit_clip_brushes(&out,&w,0));
 assert(strstr(out.p,"visible false\ncontents playerclip"));free(out.p);
}
static void test_patch_collision(void) {
 unsigned char vertices[72]={0},indices[12]={0},parts[24]={0},trees[64]={0},mats[144]={0};
 WorldGeom w={0};Buf out={0};
 /* Clockwise IW3 floor; a second partition has non-player contents. */
 f32(vertices+12,0);f32(vertices+16,2);f32(vertices+24,2);
 memcpy(vertices+36,vertices,36);indices[2]=1;indices[4]=2;indices[6]=3;indices[8]=4;indices[10]=5;
 parts[0]=parts[12]=1;u32(parts+16,1);u32(trees+60,1);trees[56]=1;
 u32(mats+68,1);u32(mats+72+68,0x2000000);
 w.cvert=vertices;w.cvert_n=6;w.cidx=indices;w.ctri_n=2;
 w.partitions=parts;w.partition_n=2;w.aabb_trees=trees;w.aabb_tree_n=2;w.cmats=mats;w.cmat_n=2;
 assert(emit_clip_triangles(&out,&w));assert(strstr(out.p,"visible 0"));
 assert(strstr(out.p,"v 0 0 0 0 0 0 0 1"));assert(strstr(out.p,"f 0 1 2"));assert(!strstr(out.p,"f 3 4 5"));free(out.p);memset(&out,0,sizeof(out));
 u32(parts+4,2);assert(!emit_clip_triangles(&out,&w));free(out.p);
}
static void test_world_image_pointer_relocations(void) {
 unsigned char data[289]={0};Cur c={0};
 /* Two material headers are TEMP. Names, texture tables and inserted pointers
    advance VIRTUAL; the first image's TEMP loadDef reserves a VIRTUAL slot. */
 u32(data,FF_NEG1);data[58]=1;u32(data+68,FF_NEG1);data[80]='a';
 data[82+7]=2;u32(data+82+8,FF_NEG1);
 u32(data+94+4,FF_NEG2);u32(data+94+32,FF_NEG1);memcpy(data+130,"atlas",6);
 u32(data+152,FF_NEG1);data[152+58]=1;u32(data+152+68,FF_NEG1);data[232]='b';
 data[234+7]=2;u32(data+234+8,FF_NEG1);u32(data+246+32,FF_NEG1);memcpy(data+282,"second",7);
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=1;world_alloc(&c,4,16);
 assert(material(&c));assert(c.o==152);assert(material(&c));assert(c.o==289);
 assert(remember_color(&c,"reference",""));c.colors[2].image_ref=0x40001039u;
 c.world_tracking=0;resolve_world_images(&c,0x1000);
 assert(c.colors[0].image_slot==0x101c);assert(c.colors[1].image_slot==0x1038);
 assert(!strcmp(c.colors[2].image,"second"));
 free(c.world_allocs);free(c.colors);free(c.mat_names);
}
static void test_ui_shared_strings(void) {
 const unsigned char data[]="@MENU_PLAY";Cur c={0};
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 c.virtual_position=128;
 assert(!strcmp(ui_string(&c,FF_NEG1),"@MENU_PLAY"));
 assert(!strcmp(ui_string(&c,0x40000081u),"@MENU_PLAY"));
 assert(c.o==sizeof(data));assert(!ui_string(&c,0x40000082u));
 assert(!ui_string(&c,0x50000081u));free(c.world_allocs);
}
static void test_ui_constant_expression(void) {
 unsigned char data[80]={0},statement[8]={0};Cur c={0};const char* text=NULL;
 u32(statement,2);u32(statement+4,FF_NEG1);
 u32(data,FF_NEG1);u32(data+4,FF_NEG1);
 u32(data+12,16); /* leading operator */
 u32(data+20,1);u32(data+24,2);u32(data+28,FF_NEG1);
 memcpy(data+32,"@MENU_JOIN_GAME",16);
 c.d=data;c.n=48;c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_statement_text(&c,statement,&text));
 assert(text && !strcmp(text,"@MENU_JOIN_GAME"));assert(c.o==48);
 free(c.world_allocs);ui_expr_free(&c);memset(&c,0,sizeof(c));
 /* A function opcode must not be mistaken for a constant label. */
 u32(data+12,31);c.d=data;c.n=48;text=(const char*)1;
 assert(ui_statement_text(&c,statement,&text));assert(!text);
 c.o=0;c.n=47;assert(!ui_statement_text(&c,statement,&text));ui_expr_free(&c);
}
static void test_ui_localization(void) {
 unsigned char data[186]={0};Cur c={0};Buf out={0};
 u32(data,FF_NEG1);u32(data+4,FF_NEG1);
 memcpy(data+8,"Join Game",10);memcpy(data+18,"MENU_JOIN_GAME",15);
 c.d=data;c.n=33;c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_localize(&c,&out));assert(!strcmp(out.p,"\"MENU_JOIN_GAME\" \"Join Game\"\n"));
 free(out.p);free(c.world_allocs);memset(&c,0,sizeof(c));
 /* Two inserted material headers, each with an inline one-byte name. */
 memset(data,0,sizeof(data));u32(data+12,FF_NEG2);u32(data+16,FF_NEG2);
 u32(data+24,FF_NEG1);u32(data+105,FF_NEG1);
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_font(&c));assert(c.o==186);assert(c.virtual_position==13);
 free(c.world_allocs);free(c.colors);free(c.mat_names);memset(&c,0,sizeof(c));
 /* A truncated second material is rejected. */
 c.d=data;c.n=160;assert(!ui_font(&c));
 free(c.world_allocs);free(c.colors);free(c.mat_names);
}
static void test_ui_localized_text(void) {
 UiLocalization loc={0};char key[]="MENU_PLAY",value[]="Play \"Game\"";
 assert(ui_loc_store(&loc,key,value));key[0]='x';value[0]='x';
 assert(!strcmp(ui_loc_text(&loc,"@MENU_PLAY"),"Play \"Game\""));
 assert(!strcmp(ui_loc_text(&loc,"@MENU_MISSING"),"@MENU_MISSING"));
 assert(!strcmp(ui_loc_text(&loc,"MENU_PLAY"),"MENU_PLAY"));
 assert(ui_loc_store(&loc,"MENU_PLAY",""));assert(!*ui_loc_text(&loc,"@MENU_PLAY"));
 ui_loc_free(&loc);assert(!loc.entries && !loc.count);
}
static void test_ui_menu_screen_anchor(void) {
 unsigned char d[300]={0};Cur c={0};Buf out={0};u32(d,FF_NEG1);f32(d+4,-164);f32(d+8,-54);f32(d+12,328);f32(d+16,108);u32(d+20,2);u32(d+24,2);memcpy(d+284,"centered",9);c.d=d;c.n=293;assert(ui_menu(&c,&out));assert(strstr(out.p,"rect 156 186 328 108"));free(out.p);
}
static void test_ui_string_table_export(void) {
 unsigned char data[64]={0};Cur c={0};Buf out={0};u32(data,FF_NEG1);u32(data+4,2);u32(data+8,1);u32(data+12,FF_NEG1);
 memcpy(data+16,"fixture",8);u32(data+24,FF_NEG1);u32(data+28,FF_NEG1);memcpy(data+32,"first",6);memcpy(data+38,"second",7);
 c.d=data;c.n=45;assert(ui_string_table_export(&c,&out));assert(strstr(out.p,"stringTable \"fixture\" 2 1"));assert(strstr(out.p,"\"first\" \"second\""));free(out.p);
 memset(&c,0,sizeof(c));memset(&out,0,sizeof(out));c.d=data;c.n=45;c.ui_assets=2;u32(data+28,0x40000001u);
 assert(!ui_string_table_export(&c,&out));free(out.p);
}
static void test_ui_action_translation(void) {
 {Buf b={0};assert(ui_emit_actions(&b,"action","play mouse_click ; uiScript quit ;","quit_popmenu"));assert(strstr(b.p,"exec \"quit\""));free(b.p);}
 {Buf b={0};assert(ui_emit_actions(&b,"action","uiScript createPlayerProfile ; close self ;","profile_create_popmenu"));assert(strstr(b.p,"cod4_profile_create"));assert(!strstr(b.p," ; close "));assert(strstr(b.p,"cod4_profile_create")<strstr(b.p,"close "));free(b.p);}
 {Buf b={0};assert(ui_emit_actions(&b,"onESC","uiScript closeMenuOnDvarNot com_playerProfile \"\" player_profile ;","player_profile"));assert(strstr(b.p,"exec_if_string_not") && strstr(b.p,"close "));free(b.p);}
 Buf out={0};
 assert(ui_emit_actions(&out,"action","uiScript openMenuOnDvar g_reloading 0 listen_popup ; uiScript openMenuOnDvarNot g_reloading 0 dead_popup ;","options_graphics"));
 assert(strstr(out.p,"exec_if_string ") && strstr(out.p,"exec_if_string_not "));
 assert(strstr(out.p,"listen_popup") && strstr(out.p,"dead_popup"));free(out.p);memset(&out,0,sizeof(out));
 assert(ui_emit_actions(&out,"action","\"open\" \"options\" ; \"setdvar\" \"test\" 1 ; \"close\" \"self\" ;","main"));
 assert(!strcmp(out.p," action { open \"options\" ; set \"test\" \"1\" ; close \"main\" ; }"));free(out.p);memset(&out,0,sizeof(out));
 assert(ui_emit_actions(&out,"onOpen","unknown { open bad ; close main ; } ; open good ; exec \"echo a;b\" ;","main"));
 assert(!strstr(out.p,"bad") && !strstr(out.p,"close"));assert(strstr(out.p,"open \"good\""));assert(strstr(out.p,"echo a;b"));free(out.p);memset(&out,0,sizeof(out));
 assert(!ui_emit_actions(&out,"action","open \"unterminated","main"));free(out.p);
}
static void test_ui_2d_material(void) {
 unsigned char image[42]={0},tex[12]={0},window[156]={0};Cur c={0};char path[128];const char* name;
 u32(image+32,FF_NEG1);memcpy(image+36,"panel",6);u32(tex+8,FF_NEG1);
 c.d=image;c.n=sizeof(image);c.ui_assets=1;
 assert(texdef(&c,tex,"ui_panel",0,0,0));assert(c.o==sizeof(image));
 assert(!strcmp(color_image(c.colors,c.color_n,"ui_panel"),"panel"));
 g_mat_addr_n=0;remember_mat_addr(0x104,"ui_panel");u32(window+152,0x40000105u);
 assert(ui_window(&c,window,&name,path,sizeof(path),0));assert(!strcmp(path,"images/panel"));
 u32(window+152,0x50000105u);assert(ui_window(&c,window,&name,path,sizeof(path),0));assert(!*path);
 free(c.colors);memset(&c,0,sizeof(c));c.d=image;c.n=sizeof(image);
 /* The world/model interpretation is unchanged: semantic 0 is not its color map. */
 assert(texdef(&c,tex,"world_panel",0,0,0));assert(!color_image(c.colors,c.color_n,"world_panel"));
 free(c.world_allocs);g_mat_addr_n=0;
}
static void test_ui_shared_image_name(void) {
 unsigned char data[43]={0};Cur c={0};char name[32];
 memcpy(data,"shared",7);u32(data+7+32,0x40000001u);
 c.d=data;c.n=sizeof(data);c.ui_assets=1;c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(!strcmp(ui_string(&c,FF_NEG1),"shared"));
 assert(image_ptr_name(&c,FF_NEG1,name,sizeof(name)));assert(!strcmp(name,"shared"));
 assert(c.o==sizeof(data));free(c.world_allocs);
}
static void test_ui_common_children(void) {
 unsigned char data[1700]={0};Cur c={0};
 u32(data+4,2);u32(data+8,1);u32(data+12,FF_NEG1);
 u32(data+16,FF_NEG1);u32(data+20,FF_NEG1);memcpy(data+24,"a\0b",4);
 c.d=data;c.n=28;assert(ui_string_table(&c));assert(c.o==28);
 c.o=0;c.n=27;assert(!ui_string_table(&c));
 c.o=0;c.n=sizeof(data);u32(data+4,0xffffffffu);assert(!ui_string_table(&c));
 memset(data,0,sizeof(data));c.o=0;c.n=8+12*33*4;u32(data+4,FF_NEG1);
 assert(ui_impact_table(&c));assert(c.o==c.n);
 c.o=0;c.n--;assert(!ui_impact_table(&c));
 c.o=0;c.n=8+12*33*4+32;u32(data+8+(12*33-1)*4,FF_NEG1);
 assert(ui_impact_table(&c));assert(c.o==c.n);
 c.o=0;c.n--;assert(!ui_impact_table(&c));
}
static void test_ui_external_material(void) {
 Cur c={0};UiLocalization context={0},lib={0};unsigned char window[156]={0};char image[128];const char* name;
 context.materials=&lib;c.ui_localization=&context;
 assert(ui_loc_store(&lib,"shared_panel","panel_image"));
 g_mat_addr_n=0;remember_mat_addr(0x104,",shared_panel");u32(window+152,0x40000105u);
 assert(ui_window(&c,window,&name,image,sizeof(image),0));assert(!strcmp(image,"images/panel_image"));
 ui_loc_free(&lib);g_mat_addr_n=0;
}
static void test_ui_shared_expressions(void) {
 unsigned char data[45]={0},s[8]={0};Cur c={0};const char* text=NULL;Buf expr={0};
 u32(s,2);u32(s+4,FF_NEG1);u32(data,FF_NEG1);u32(data+4,FF_NEG1);
 u32(data+12,16);u32(data+20,1);u32(data+24,2);u32(data+28,FF_NEG1);memcpy(data+32,"Play",5);
 u32(data+37,0x40000009u);u32(data+41,0x40000015u);
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_statement_expr(&c,s,&text,&expr));assert(text && !strcmp(text,"Play"));assert(c.o==37);
 assert(strstr(expr.p,"op 16 string \"Play\""));free(expr.p);memset(&expr,0,sizeof(expr));
 assert(ui_statement_expr(&c,s,&text,&expr));assert(text && !strcmp(text,"Play"));assert(c.o==45);free(expr.p);
 u32(s+4,0x40000001u);assert(ui_statement_text(&c,s,&text));assert(!strcmp(text,"Play") && c.o==45);
 u32(s,3);assert(!ui_statement_text(&c,s,&text));
 ui_expr_free(&c);free(c.world_allocs);
}
static void test_ui_text_alignment(void) {
 unsigned char data[377]={0};Cur c={0};Buf out={0};
 u32(data+196,10);u32(data+224,FF_NEG1);memcpy(data+372,"Play",5);
 c.d=data;c.n=sizeof(data);assert(ui_item(&c,&out,0,"main"));
 assert(strstr(out.p,"iw3TextAlign 10 textalign right"));free(out.p);ui_expr_free(&c);
}
static void test_ui_multi_widget_bindings(void) {
 unsigned char data[1157]={0};Cur c={0};Buf out={0};
 u32(data+180,12);u32(data+268,FF_NEG1);u32(data+300,FF_NEG1);memcpy(data+372,"ui_test",8);
 u32(data+380,FF_NEG1);u32(data+384,FF_NEG1);u32(data+380+128,FF_NEG1);u32(data+380+132,FF_NEG1);
 u32(data+380+384,2);u32(data+380+388,1);memcpy(data+772,"Low\0High\0" "0\0" "1",13);
 /* A second item reuses the definition and the dvar XString. */
 u32(data+785+180,12);u32(data+785+268,0x40000175u);u32(data+785+300,0x4000017du);
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_item(&c,&out,0,"test"));assert(c.o==785);
 assert(strstr(out.p,"choices { \"0\" \"Low\" \"1\" \"High\" }"));
 assert(strstr(out.p,"type multi") && strstr(out.p,"cvar \"ui_test\""));
 out.n=0;out.p[0]=0;assert(ui_item(&c,&out,1,"test"));assert(c.o==sizeof(data));
 assert(strstr(out.p,"choices { \"0\" \"Low\" \"1\" \"High\" }"));
 free(out.p);free(c.world_allocs);ui_widgets_free(&c);ui_expr_free(&c);
}
static void test_ui_edit_widget_limits(void) {
 unsigned char data[32]={0};Cur c={0};Buf out={0};int known=0;
 f32(data,-10);f32(data+4,10);u32(data+16,8);
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_widget(&c,&out,10,FF_NEG1,&known));assert(known==2);
 assert(strstr(out.p,"min -10 max 10 step 0 numericRange 1") && strstr(out.p,"maxChars 8"));
 assert(ui_widget(&c,&out,4,0x40000001u,&known));assert(known==2 && c.o==32);
 free(out.p);free(c.world_allocs);ui_widgets_free(&c);
 memset(&c,0,sizeof(c));c.d=data;c.n=31;memset(&out,0,sizeof(out));assert(!ui_widget(&c,&out,99,FF_NEG1,&known));assert(!ui_widget(&c,&out,4,FF_NEG1,&known));free(out.p);
 /* Enum names are byte-aligned XStrings, not aligned structure headers. */
 memset(&c,0,sizeof(c));memcpy(data,"Enum",5);c.d=data;c.n=5;c.virtual_position=1;
 c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_widget(&c,&out,13,FF_NEG1,&known));assert(c.virtual_position==6);
 free(c.world_allocs);
}
static void test_ui_numeric_choices_and_screen_anchors(void) {
 unsigned char data[396]={0};Cur c={0};Buf out={0};int known;
 u32(data,FF_NEG1);u32(data+4,FF_NEG1);f32(data+256,-1);f32(data+260,0.5f);
 u32(data+384,2);memcpy(data+392,"A\0B",4);
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_widget(&c,&out,12,FF_NEG1,&known));
 assert(strstr(out.p,"choices { \"-1\" \"A\" \"0.5\" \"B\" }"));
 assert(ui_rect_coord(-74,2,640)==246);assert(ui_rect_coord(-20,3,480)==460);
 assert(ui_rect_coord(-107,4,640)==-107);
 free(out.p);free(c.world_allocs);ui_widgets_free(&c);
}
static void test_ui_conditional_dvar_commands(void) {
 Buf out={0};
 assert(ui_emit_actions(&out,"onOpen","\"execOnDvarIntValue\" \"mode\" 1 \"set a 2; set b 3\" ;","test"));
 assert(strstr(out.p,"exec_if_int") && strstr(out.p,"set a 2; set b 3"));
 free(out.p);
}
static void test_ui_item_uses_parent_relative_rect(void) {
 unsigned char data[372]={0};Cur c={0};Buf out={0};
 f32(data+4,234);f32(data+8,252);f32(data+12,250);f32(data+16,18);
 f32(data+28,4);f32(data+32,26);f32(data+36,250);f32(data+40,18);u32(data+76,0x800000u);
 c.d=data;c.n=sizeof(data);assert(ui_item(&c,&out,0,"popup"));
 assert(strstr(out.p,"textWrap 1"));assert(strstr(out.p,"rect 4 26 250 18"));assert(!strstr(out.p,"rect 234 252"));free(out.p);
 memset(&out,0,sizeof(out));memset(&c,0,sizeof(c));c.d=data;c.n=sizeof(data);u32(data+44,2);u32(data+48,2);assert(ui_item_relative(&c,&out,0,"centered",320,240));assert(strstr(out.p,"rect 4 26 250 18"));free(out.p);
 memset(&out,0,sizeof(out));memset(&c,0,sizeof(c));c.d=data;c.n=sizeof(data);u32(data+44,0);u32(data+48,0);assert(ui_item_relative(&c,&out,0,"centered",320,240));assert(strstr(out.p,"rect 4 26 250 18"));free(out.p);
}
static void test_ui_video_enum(void) {
 const unsigned char data[]="r_mode";Cur c={0};Buf out={0};int known;
 c.d=data;c.n=sizeof(data);c.world_tracking=c.world_valid=c.virtual_absolute=1;
 assert(ui_widget(&c,&out,13,FF_NEG1,&known));assert(known==1);
 assert(strstr(out.p,"feeder \"cod4_video_modes\""));assert(c.o==sizeof(data));
 assert(ui_widget(&c,&out,13,0x40000001u,&known));assert(known==1 && c.o==sizeof(data));
 free(out.p);free(c.world_allocs);
}
static void test_asset_name_copy(void) {
 const unsigned char data[]="wc/Brick@red+$white";Cur c={0};char out[64];c.d=data;c.n=sizeof(data);
 assert(stream_str(&c,out,sizeof(out)) && !strcmp(out,"wc/brick@red+$white"));
 memset(out,'x',sizeof(out));assert(!stream_str(&c,out,8) && !out[0]);
 c.n=sizeof(data)-1;assert(!stream_str(&c,out,sizeof(out)) && !out[0]);
}
static void test_zone_children(void) {
 unsigned char d[3000]={0};Cur c={0};Buf out={0};
 c.d=d;c.n=sizeof(d);
 u32(d,FF_NEG1);u32(d+4,2);u32(d+8,FF_NEG1);memcpy(d+12,"r\0abc",5);
 assert(raw_file(&c) && c.o==17);c.o=0;c.n=16;assert(!raw_file(&c));
 memset(d,0,sizeof(d));c.o=0;c.n=sizeof(d);
 u32(d+84,FF_NEG1);u32(d+76,FF_NEG1);u32(d+36,3);d[14]=1;d[15]=1;
 assert(anim_parts(&c) && c.o==102);c.o=0;c.n=101;assert(!anim_parts(&c));
 memset(d,0,sizeof(d));c.o=0;c.n=sizeof(d);
 u32(d+16,0xffffffffu);u32(d+20,1);assert(!fx_effect(&c));
 memset(d,0,sizeof(d));c.o=0;c.n=sizeof(d);
 u32(d+16,1);u32(d+28,FF_NEG1);d[32+176]=8;d[32+177]=1;
 u32(d+32+188,FF_NEG1);memcpy(d+284,"sound",6);
 assert(fx_effect(&c) && c.o==290);c.o=0;c.n=289;assert(!fx_effect(&c));
 memset(d,0,sizeof(d));c.o=0;c.n=sizeof(d);
 u32(d+4,FF_NEG1);u32(d+8,1);u32(d+12+16,FF_NEG1);d[104]=2;
 u32(d+108,FF_NEG1);u32(d+112,FF_NEG1);memcpy(d+116,"dir\0file",9);
 assert(sound_alias(&c) && c.o==125);c.o=0;c.n=124;assert(!sound_alias(&c));
 memset(d,0,sizeof(d));c.o=0;c.n=2168;assert(weapon_def(&c) && c.o==2168);
 c.o=0;c.n=2167;assert(!weapon_def(&c));
 memset(d,0,sizeof(d));c.o=0;c.n=sizeof(d);u32(d,FF_NEG1);memcpy(d+284,"test",5);
 assert(ui_menu(&c,&out));assert(strstr(out.p,"name \"test\""));free(out.p);
 c.o=0;c.n=283;assert(!ui_menu(&c,&out));
}
static int check_retail(const char* path,int ui,const char* dest,const char* localized_path) {
 FILE* f=fopen(path,"rb");long n;unsigned char *d,*out=NULL;unsigned out_n=0;int ok;
 if(!f)return 2;
 if(fseek(f,0,SEEK_END) || (n=ftell(f))<0 || (unsigned long)n>0xffffffffu || fseek(f,0,SEEK_SET)){fclose(f);return 2;}
 d=(unsigned char*)malloc((size_t)n);if(!d){fclose(f);return 2;}
 if(fread(d,1,(size_t)n,f)!=(size_t)n){free(d);fclose(f);return 2;}fclose(f);
 if(localized_path) {
  unsigned char* ld;long ln;f=fopen(localized_path,"rb");
  if(!f){free(d);return 2;}
  if(fseek(f,0,SEEK_END) || (ln=ftell(f))<0 || (unsigned long)ln>0xffffffffu || fseek(f,0,SEEK_SET)){fclose(f);free(d);return 2;}
  ld=(unsigned char*)malloc((size_t)ln);if(!ld){fclose(f);free(d);return 2;}
  if(fread(ld,1,(size_t)ln,f)!=(size_t)ln){free(ld);fclose(f);free(d);return 2;}fclose(f);
  ok=cod4_ff_to_localized_menu_layout(d,(unsigned)n,ld,(unsigned)ln,&out,&out_n);free(ld);
 } else
 ok=ui==4?cod4_ff_to_string_tables(d,(unsigned)n,&out,&out_n):ui==3?cod4_ff_to_ui_materials(d,(unsigned)n,&out,&out_n):ui==2?cod4_ff_to_localization(d,(unsigned)n,&out,&out_n):ui?cod4_ff_to_menu_layout(d,(unsigned)n,&out,&out_n):cod4_ff_to_hxmap(d,(unsigned)n,path,&out,&out_n);
 if(ok && dest){f=fopen(dest,"wb");if(!f)ok=0;else {ok=fwrite(out,1,out_n,f)==out_n;fclose(f);}}
 printf("%s: %s, %u output bytes\n",path,ok?"ok":"FAILED",out_n);free(out);free(d);return ok?0:1;
}
int main(int argc,char** argv){
 if(argc>=4 && !strcmp(argv[1],"--localized-menu"))return check_retail(argv[2],1,argc>4?argv[4]:NULL,argv[3]);
 if(argc>=3 && (!strcmp(argv[1],"--ff") || !strcmp(argv[1],"--ui") || !strcmp(argv[1],"--loc") || !strcmp(argv[1],"--ui-materials") || !strcmp(argv[1],"--tables")))return check_retail(argv[2],!strcmp(argv[1],"--tables")?4:!strcmp(argv[1],"--ui-materials")?3:!strcmp(argv[1],"--loc")?2:!strcmp(argv[1],"--ui"),argc>3?argv[3]:NULL,NULL);
 test_ui_string_table_export();test_ui_menu_screen_anchor();test_zone_children();test_asset_name_copy();test_ui_shared_strings();test_ui_constant_expression();test_ui_localization();test_ui_localized_text();test_ui_action_translation();test_ui_2d_material();test_ui_shared_image_name();test_ui_common_children();test_ui_external_material();test_ui_shared_expressions();test_ui_text_alignment();test_ui_multi_widget_bindings();test_ui_edit_widget_limits();test_ui_numeric_choices_and_screen_anchors();test_ui_conditional_dvar_commands();test_ui_item_uses_parent_relative_rect();test_ui_video_enum();test_world_image_pointer_relocations();test_patch_collision();test_surface_asset_slots();test_playerclip_brush();test_world_vertex_export();
 ColorMap row={0};char out[160];row.tint[0]=row.tint[1]=row.tint[2]=2;
 assert(decorate_image("test",&row,out,sizeof(out)));assert(strstr(out,"*{2.000"));
 test_world_layers_keep_import_policy();test_shared_image_keeps_state();test_shared_model_image();test_visible_pass_state();test_collision_coordinates();test_collision_contents();test_authored_decals();test_instance_export();test_lod();test_alpha(0x800|2|(1<<4),0);test_alpha(0x3000|2|(1<<4),0);
 test_alpha(0x800|5|(6<<4),1);test_ladder();puts("cod4_ffmap_regression_test: ok");return 0;
}
