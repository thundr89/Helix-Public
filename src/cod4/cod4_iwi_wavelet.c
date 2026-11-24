/* IWI wavelet decoder adapted from OpenAssetTools IwiWaveletDecoder.cpp.
 * Copyright OpenAssetTools contributors. SPDX-License-Identifier: GPL-3.0-only
 * Upstream: https://github.com/Laupetin/OpenAssetTools
 * Revision: 2826174cf74dd6a94bcc0a1b96c1967aa3d8d760
 * C99 adaptation: Helix-Public contributors, 2026.
 * Kept in the GPL asset adapter; no GPL implementation enters the engine.
 */
#include "cod4_iwi_wavelet.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define ESCAPE_VALUE INT16_MIN
#define LOOKUP_SIZE 4096u
typedef struct Codeword {uint16_t code;uint8_t bits;int16_t value;} Codeword;
typedef struct Decode {int16_t value;uint8_t bits;} Decode;
static const Codeword blue_codes[] = {
    {0x001, 3, 0},
    {0x004, 5, 4},
    {0x005, 5, 2},
    {0x007, 5, 1},
    {0x00A, 5, 3},
    {0x014, 5, -4},
    {0x015, 5, -2},
    {0x017, 5, -1},
    {0x01A, 5, -3},
    {0x000, 6, 12},
    {0x002, 6, 10},
    {0x003, 6, 7},
    {0x006, 6, 9},
    {0x00B, 6, 6},
    {0x018, 6, 11},
    {0x01E, 6, 8},
    {0x01F, 6, 5},
    {0x020, 6, -12},
    {0x022, 6, -10},
    {0x023, 6, -7},
    {0x026, 6, -9},
    {0x02B, 6, -6},
    {0x038, 6, -11},
    {0x03C, 6, ESCAPE_VALUE},
    {0x03E, 6, -8},
    {0x03F, 6, -5},
    {0x00F, 7, 13},
    {0x012, 7, 19},
    {0x016, 7, 18},
    {0x01B, 7, 14},
    {0x028, 7, 21},
    {0x02C, 7, 20},
    {0x02D, 7, 16},
    {0x02E, 7, 17},
    {0x030, 7, 22},
    {0x03D, 7, 15},
    {0x04F, 7, -13},
    {0x052, 7, -19},
    {0x056, 7, -18},
    {0x05B, 7, -14},
    {0x068, 7, -21},
    {0x06C, 7, -20},
    {0x06D, 7, -16},
    {0x06E, 7, -17},
    {0x070, 7, -22},
    {0x07D, 7, -15},
    {0x008, 8, 34},
    {0x00D, 8, 28},
    {0x00E, 8, 29},
    {0x013, 8, 26},
    {0x01D, 8, 27},
    {0x02F, 8, 23},
    {0x033, 8, 25},
    {0x03B, 8, 24},
    {0x048, 8, 33},
    {0x04C, 8, 32},
    {0x05C, 8, 31},
    {0x072, 8, 30},
    {0x088, 8, -34},
    {0x08D, 8, -28},
    {0x08E, 8, -29},
    {0x093, 8, -26},
    {0x09D, 8, -27},
    {0x0AF, 8, -23},
    {0x0B3, 8, -25},
    {0x0BB, 8, -24},
    {0x0C8, 8, -33},
    {0x0CC, 8, -32},
    {0x0DC, 8, -31},
    {0x0F2, 8, -30},
    {0x00C, 9, 47},
    {0x01C, 9, 46},
    {0x032, 9, 45},
    {0x036, 9, 44},
    {0x050, 9, 48},
    {0x076, 9, 43},
    {0x07B, 9, 37},
    {0x090, 9, 49},
    {0x0CD, 9, 40},
    {0x0CE, 9, 41},
    {0x0D3, 9, 38},
    {0x0DD, 9, 39},
    {0x0EF, 9, 35},
    {0x0F6, 9, 42},
    {0x0FB, 9, 36},
    {0x10C, 9, -47},
    {0x11C, 9, -46},
    {0x132, 9, -45},
    {0x136, 9, -44},
    {0x150, 9, -48},
    {0x176, 9, -43},
    {0x17B, 9, -37},
    {0x190, 9, -49},
    {0x1CD, 9, -40},
    {0x1CE, 9, -41},
    {0x1D3, 9, -38},
    {0x1DD, 9, -39},
    {0x1EF, 9, -35},
    {0x1F6, 9, -42},
    {0x1FB, 9, -36},
    {0x010, 10, 65},
    {0x04D, 10, 56},
    {0x04E, 10, 57},
    {0x05D, 10, 55},
    {0x08C, 10, 62},
    {0x09C, 10, 61},
    {0x110, 10, 64},
    {0x153, 10, 53},
    {0x15D, 10, 54},
    {0x16F, 10, 50},
    {0x173, 10, 52},
    {0x19C, 10, 60},
    {0x1B2, 10, 59},
    {0x1B6, 10, 58},
    {0x1D0, 10, 63},
    {0x1F3, 10, 51},
    {0x210, 10, -65},
    {0x24D, 10, -56},
    {0x24E, 10, -57},
    {0x25D, 10, -55},
    {0x28C, 10, -62},
    {0x29C, 10, -61},
    {0x310, 10, -64},
    {0x353, 10, -53},
    {0x35D, 10, -54},
    {0x36F, 10, -50},
    {0x373, 10, -52},
    {0x39C, 10, -60},
    {0x3B2, 10, -59},
    {0x3B6, 10, -58},
    {0x3D0, 10, -63},
    {0x3F3, 10, -51},
    {0x053, 11, 70},
    {0x06F, 11, 66},
    {0x073, 11, 69},
    {0x0B2, 11, 77},
    {0x0B6, 11, 75},
    {0x0D0, 11, 81},
    {0x14E, 11, 73},
    {0x18C, 11, 79},
    {0x273, 11, 68},
    {0x2B2, 11, 76},
    {0x2B6, 11, 74},
    {0x2D0, 11, 80},
    {0x2F3, 11, 67},
    {0x34D, 11, 71},
    {0x34E, 11, 72},
    {0x38C, 11, 78},
    {0x453, 11, -70},
    {0x46F, 11, -66},
    {0x473, 11, -69},
    {0x4B2, 11, -77},
    {0x4B6, 11, -75},
    {0x4D0, 11, -81},
    {0x54E, 11, -73},
    {0x58C, 11, -79},
    {0x673, 11, -68},
    {0x6B2, 11, -76},
    {0x6B6, 11, -74},
    {0x6D0, 11, -80},
    {0x6F3, 11, -67},
    {0x74D, 11, -71},
    {0x74E, 11, -72},
    {0x78C, 11, -78},
    {0x0F3, 12, 85},
    {0x14D, 12, 89},
    {0x253, 12, 87},
    {0x26F, 12, 83},
    {0x4F3, 12, 84},
    {0x54D, 12, 88},
    {0x653, 12, 86},
    {0x66F, 12, 82},
    {0x8F3, 12, -85},
    {0x94D, 12, -89},
    {0xA53, 12, -87},
    {0xA6F, 12, -83},
    {0xCF3, 12, -84},
    {0xD4D, 12, -88},
    {0xE53, 12, -86},
    {0xE6F, 12, -82},
};
static const Codeword red_green_codes[] = {
    {0x003, 2, 0},
    {0x002, 3, 1},
    {0x006, 3, -1},
    {0x001, 4, 2},
    {0x009, 4, -2},
    {0x004, 5, 4},
    {0x00D, 5, 3},
    {0x014, 5, -4},
    {0x01D, 5, -3},
    {0x00C, 6, 6},
    {0x010, 6, 7},
    {0x015, 6, 5},
    {0x02C, 6, -6},
    {0x030, 6, -7},
    {0x035, 6, -5},
    {0x018, 7, 10},
    {0x01C, 7, 9},
    {0x020, 7, 11},
    {0x025, 7, 8},
    {0x058, 7, -10},
    {0x05C, 7, -9},
    {0x060, 7, -11},
    {0x065, 7, -8},
    {0x068, 7, ESCAPE_VALUE},
    {0x038, 8, 14},
    {0x040, 8, 16},
    {0x045, 8, 12},
    {0x048, 8, 15},
    {0x07C, 8, 13},
    {0x0B8, 8, -14},
    {0x0C0, 8, -16},
    {0x0C5, 8, -12},
    {0x0C8, 8, -15},
    {0x0FC, 8, -13},
    {0x080, 9, 22},
    {0x085, 9, 17},
    {0x088, 9, 21},
    {0x0A8, 9, 20},
    {0x0BC, 9, 18},
    {0x0F8, 9, 19},
    {0x180, 9, -22},
    {0x185, 9, -17},
    {0x188, 9, -21},
    {0x1A8, 9, -20},
    {0x1BC, 9, -18},
    {0x1F8, 9, -19},
    {0x000, 10, 30},
    {0x03C, 10, 25},
    {0x078, 10, 26},
    {0x100, 10, 29},
    {0x105, 10, 23},
    {0x108, 10, 28},
    {0x128, 10, 27},
    {0x13C, 10, 24},
    {0x200, 10, -30},
    {0x23C, 10, -25},
    {0x278, 10, -26},
    {0x300, 10, -29},
    {0x305, 10, -23},
    {0x308, 10, -28},
    {0x328, 10, -27},
    {0x33C, 10, -24},
    {0x005, 11, 31},
    {0x008, 11, 37},
    {0x028, 11, 35},
    {0x178, 11, 33},
    {0x208, 11, 36},
    {0x228, 11, 34},
    {0x378, 11, 32},
    {0x405, 11, -31},
    {0x408, 11, -37},
    {0x428, 11, -35},
    {0x578, 11, -33},
    {0x608, 11, -36},
    {0x628, 11, -34},
    {0x778, 11, -32},
    {0x205, 12, 39},
    {0x605, 12, 38},
    {0xA05, 12, -39},
    {0xE05, 12, -38},
};
static const Codeword alpha_codes[] = {
    {0x001, 1, 0},
    {0x000, 4, ESCAPE_VALUE},
    {0x002, 4, 1},
    {0x00A, 4, -1},
    {0x00C, 5, 2},
    {0x01C, 5, -2},
    {0x016, 6, 3},
    {0x018, 6, 4},
    {0x036, 6, -3},
    {0x038, 6, -4},
    {0x004, 7, 7},
    {0x02E, 7, 5},
    {0x034, 7, 6},
    {0x044, 7, -7},
    {0x06E, 7, -5},
    {0x074, 7, -6},
    {0x006, 8, 11},
    {0x008, 8, 14},
    {0x014, 8, 12},
    {0x01E, 8, 9},
    {0x048, 8, 15},
    {0x066, 8, 10},
    {0x068, 8, 13},
    {0x07E, 8, 8},
    {0x086, 8, -11},
    {0x088, 8, -14},
    {0x094, 8, -12},
    {0x09E, 8, -9},
    {0x0C8, 8, -15},
    {0x0E6, 8, -10},
    {0x0E8, 8, -13},
    {0x0FE, 8, -8},
    {0x028, 9, 23},
    {0x046, 9, 19},
    {0x054, 9, 20},
    {0x08E, 9, 17},
    {0x0A4, 9, 22},
    {0x0A8, 9, 24},
    {0x0C6, 9, 18},
    {0x0DE, 9, 16},
    {0x0E4, 9, 21},
    {0x128, 9, -23},
    {0x146, 9, -19},
    {0x154, 9, -20},
    {0x18E, 9, -17},
    {0x1A4, 9, -22},
    {0x1A8, 9, -24},
    {0x1C6, 9, -18},
    {0x1DE, 9, -16},
    {0x1E4, 9, -21},
    {0x00E, 10, 29},
    {0x024, 10, 37},
    {0x026, 10, 31},
    {0x04E, 10, 28},
    {0x064, 10, 35},
    {0x0BE, 10, 32},
    {0x0D4, 10, 33},
    {0x124, 10, 36},
    {0x126, 10, 30},
    {0x13E, 10, 25},
    {0x15E, 10, 26},
    {0x164, 10, 34},
    {0x1A6, 10, 127},
    {0x1CE, 10, 27},
    {0x1D4, 10, 128},
    {0x20E, 10, -29},
    {0x224, 10, -37},
    {0x226, 10, -31},
    {0x24E, 10, -28},
    {0x264, 10, -35},
    {0x2BE, 10, -32},
    {0x2D4, 10, -33},
    {0x324, 10, -36},
    {0x326, 10, -30},
    {0x33E, 10, -25},
    {0x35E, 10, -26},
    {0x364, 10, -34},
    {0x3A6, 10, -127},
    {0x3CE, 10, -27},
    {0x3D4, 10, -128},
    {0x03E, 11, 41},
    {0x05E, 11, 43},
    {0x0A6, 11, 50},
    {0x0CE, 11, 48},
    {0x10E, 11, 49},
    {0x14E, 11, 64},
    {0x1BE, 11, 39},
    {0x23E, 11, 40},
    {0x25E, 11, 42},
    {0x2A6, 11, 47},
    {0x2CE, 11, 44},
    {0x30E, 11, 46},
    {0x34E, 11, 45},
    {0x3BE, 11, 38},
    {0x43E, 11, -41},
    {0x45E, 11, -43},
    {0x4A6, 11, -50},
    {0x4CE, 11, -48},
    {0x50E, 11, -49},
    {0x54E, 11, -64},
    {0x5BE, 11, -39},
    {0x63E, 11, -40},
    {0x65E, 11, -42},
    {0x6A6, 11, -47},
    {0x6CE, 11, -44},
    {0x70E, 11, -46},
    {0x74E, 11, -45},
    {0x7BE, 11, -38},
};
typedef struct Reader {
    const unsigned char* data;unsigned size,byte;uint64_t bit;int started;
} Reader;
static unsigned get16(const unsigned char* p){return (unsigned)p[0]|((unsigned)p[1]<<8);}
static uint32_t get32(const unsigned char* p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int build_lookup(Decode* out,const Codeword* codes,unsigned n){
    unsigned i,j;memset(out,0,LOOKUP_SIZE*sizeof(*out));
    for(i=0;i<n;i++){
        unsigned step;
        if(!codes[i].bits || codes[i].bits>12)return 0;
        step=1u<<codes[i].bits;if(codes[i].code>=step)return 0;
        for(j=codes[i].code;j<LOOKUP_SIZE;j+=step){
            if(out[j].bits)return 0;
            out[j].bits=codes[i].bits;out[j].value=codes[i].value;
        }
    }
    return 1;
}
static unsigned peek(Reader* r,unsigned n){
    unsigned i,value=0;
    if(!r->started){r->bit=(uint64_t)r->byte*8u;r->started=1;}
    for(i=0;i<n && r->bit+i<(uint64_t)r->size*8u;i++)
        value|=((unsigned)(r->data[(r->bit+i)/8u]>>((r->bit+i)%8u))&1u)<<i;
    return value;
}
static int bits(Reader* r,unsigned n,unsigned* value){
    *value=peek(r,n);if(r->bit+n>(uint64_t)r->size*8u)return 0;r->bit+=n;return 1;
}
static int value(Reader* r,const Decode* table,unsigned escape_bits,int bias,int* result){
    Decode d=table[peek(r,12)];unsigned raw;
    if(!d.bits || !bits(r,d.bits,&raw))return 0;
    *result=d.value;
    if(d.value==ESCAPE_VALUE){if(!bits(r,escape_bits,&raw))return 0;*result=(int)raw-bias;}
    return 1;
}
static int coefficients(Reader* r,const Decode* table,unsigned n,int bias,int c[3]){
    unsigned i;for(i=0;i<3;i++)if(!value(r,table,n,bias,c+i))return 0;return 1;
}
/* Define negative division explicitly instead of relying on signed right shift. */
static int half(int n){return n>=0?n/2:-((1-n)/2);}
static void reconstruct(const unsigned char* source,unsigned char* dst,unsigned bpp,unsigned stride,unsigned channel,unsigned parity,const int c[3]){
    int base=2*source[channel],h=c[0],v=c[1],d=c[2];
    dst[channel]=(unsigned char)((int)parity+half(d+v+h+base));
    dst[bpp+channel]=(unsigned char)half(h+base-d-v);
    dst[stride+channel]=(unsigned char)half(v-d+base-h);
    dst[stride+bpp+channel]=(unsigned char)half(base-h-v+d);
}
static int decode_level(Reader* r,Decode* tables,const unsigned char* source,unsigned char* dst,unsigned w,unsigned h,unsigned channels,unsigned bpp){
    unsigned x,y,c,parity,delta;unsigned char* adjusted=NULL;int ok=0;
    const Decode *blue=tables,*rg=tables+LOOKUP_SIZE,*alpha=tables+2u*LOOKUP_SIZE;
    if(w<=1 || h<=1){
        if(r->started)return 0;
        for(x=0;x<w*h;x++){
            for(c=0;c<channels;c++){if(r->byte>=r->size)return 0;dst[x*bpp+c]=r->data[r->byte++];}
            for(c=channels;c<bpp;c++)dst[x*bpp+c]=255;
        }
        return 1;
    }
    if(!source || !bits(r,1,&delta))return 0;
    if(delta){
        unsigned pixels=w*h/4u;
        adjusted=(unsigned char*)malloc(pixels*bpp);if(!adjusted)return 0;
        memcpy(adjusted,source,pixels*bpp);source=adjusted;
        for(x=0;x<pixels;x++)for(c=0;c<channels;c++){
            int d;if(!value(r,alpha,9,255,&d))goto done;
            adjusted[x*bpp+c]=(unsigned char)(adjusted[x*bpp+c]+d);
        }
    }
    for(y=0;y<h;y+=2)for(x=0;x<w;x+=2){
        unsigned char* out=dst+(y*w+x)*bpp;
        const unsigned char* in=source+((y/2u)*(w/2u)+x/2u)*bpp;
        int blue_c[3]={0,0,0},other[3];
        if(channels!=1){
            if(!bits(r,1,&parity) || !coefficients(r,blue,9,255,blue_c))goto done;
            reconstruct(in,out,bpp,w*bpp,0,parity,blue_c);
            if(channels>=3)for(c=1;c<=2;c++){
                unsigned k;if(!bits(r,1,&parity) || !coefficients(r,rg,10,510,other))goto done;
                for(k=0;k<3;k++)other[k]+=blue_c[k];
                reconstruct(in,out,bpp,w*bpp,c,parity,other);
            }
        }
        if(channels==3){out[3]=out[bpp+3]=out[w*bpp+3]=out[w*bpp+bpp+3]=255;}
        else {
            if(!bits(r,1,&parity) || !coefficients(r,alpha,9,255,other))goto done;
            reconstruct(in,out,bpp,w*bpp,channels-1u,parity,other);
        }
    }
    ok=1;
done:
    free(adjusted);return ok;
}
int cod4_iwi_wavelet_to_tga(const unsigned char* data,unsigned size,unsigned char** out,unsigned* out_size){
    unsigned format,w,h,channels,bpp,levels=1,mw,mh,i,pixels;int level,ok=0;
    Decode* tables=NULL;unsigned char *previous=NULL,*current=NULL,*tga=NULL;Reader reader;
    if(!out || !out_size)return 0;*out=NULL;*out_size=0;
    if(!data || size<28 || size>256u*1024u*1024u || memcmp(data,"IWi\6",4))return 0;
    format=data[4];w=get16(data+6);h=get16(data+8);
    if(format<6 || format>10 || (data[5]&4u) || get16(data+10)!=1 || get32(data+12)!=size)return 0;
    if(!w || !h || (w&(w-1)) || (h&(h-1)) || (uint64_t)w*h*4u>128u*1024u*1024u)return 0;
    if((data[5]&2u) && w>1 && h>1)return 0;
    channels=format==6?4:format==7?3:format==8?2:1;bpp=channels>=3?4:channels;
    if(!(data[5]&2u)){unsigned n=w>h?w:h;while(n>1){n>>=1;levels++;}}
    tables=(Decode*)malloc(3u*LOOKUP_SIZE*sizeof(*tables));if(!tables)goto done;
    if(!build_lookup(tables,blue_codes,sizeof(blue_codes)/sizeof(*blue_codes)) ||
       !build_lookup(tables+LOOKUP_SIZE,red_green_codes,sizeof(red_green_codes)/sizeof(*red_green_codes)) ||
       !build_lookup(tables+2u*LOOKUP_SIZE,alpha_codes,sizeof(alpha_codes)/sizeof(*alpha_codes)))goto done;
    memset(&reader,0,sizeof(reader));reader.data=data+28;reader.size=size-28;
    for(level=(int)levels-1;level>=0;level--){
        mw=w>>(unsigned)level;mh=h>>(unsigned)level;if(!mw)mw=1;if(!mh)mh=1;
        current=(unsigned char*)malloc(mw*mh*bpp);if(!current)goto done;
        if(!decode_level(&reader,tables,previous,current,mw,mh,channels,bpp))goto done;
        free(previous);previous=current;current=NULL;
    }
    pixels=w*h;tga=(unsigned char*)calloc(1,18u+pixels*4u);if(!tga)goto done;
    tga[2]=2;tga[12]=(unsigned char)w;tga[13]=(unsigned char)(w>>8);
    tga[14]=(unsigned char)h;tga[15]=(unsigned char)(h>>8);tga[16]=32;tga[17]=0x28;
    for(i=0;i<pixels;i++){
        unsigned char* p=tga+18+i*4u;const unsigned char* src=previous+i*bpp;
        if(channels>=3){memcpy(p,src,4);}
        else if(format==10){p[0]=p[1]=p[2]=255;p[3]=src[0];}
        else {p[0]=p[1]=p[2]=src[0];p[3]=channels==2?src[1]:255;}
    }
    *out=tga;*out_size=18u+pixels*4u;tga=NULL;ok=1;
done:
    free(tables);free(previous);free(current);free(tga);return ok;
}
