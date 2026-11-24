/* Wavelet adapter tests: raw seeds, one reconstruction level and retail probes. */
#include "cod4_iwi_wavelet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
static void put32(unsigned char* p,unsigned n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void header(unsigned char* b,unsigned size,unsigned format,unsigned w,unsigned h,unsigned flags){
 memset(b,0,size);memcpy(b,"IWi\6",4);b[4]=format;b[5]=flags;b[6]=w;b[7]=w>>8;b[8]=h;b[9]=h>>8;b[10]=1;
 for(unsigned i=0;i<4;i++)put32(b+12+i*4,size);
}
static void emit(unsigned char* b,unsigned* bit,unsigned n,unsigned value){
 for(unsigned i=0;i<n;i++,(*bit)++)b[*bit/8]|=((value>>i)&1u)<<(*bit%8);
}
int main(int argc,char** argv){
 unsigned char input[64],*out=NULL;unsigned size=0,bit=0;
 if(argc==3){
  FILE* f=fopen(argv[1],"rb");long n;if(!f)return 2;
  if(fseek(f,0,SEEK_END) || (n=ftell(f))<28 || n>256L*1024L*1024L || fseek(f,0,SEEK_SET)){fclose(f);return 2;}
  unsigned char* data=malloc((size_t)n);if(!data){fclose(f);return 2;}
  int ok=fread(data,1,(size_t)n,f)==(size_t)n;fclose(f);
  if(ok)ok=cod4_iwi_wavelet_to_tga(data,(unsigned)n,&out,&size);free(data);
  if(ok){f=fopen(argv[2],"wb");if(!f)ok=0;else{ok=fwrite(out,1,size,f)==size;fclose(f);}}
  free(out);printf("wavelet retail decode: %s, %u bytes\n",ok?"ok":"FAILED",size);return ok?0:1;
 }
 for(unsigned format=6;format<=10;format++){
  unsigned channels=format==6?4:format==7?3:format==8?2:1;
  header(input,28+channels,format,1,1,2);for(unsigned i=0;i<channels;i++)input[28+i]=(unsigned char)(20+i*30);
  assert(cod4_iwi_wavelet_to_tga(input,28+channels,&out,&size));assert(size==22 && out[16]==32 && out[17]==0x28);
  if(format==6)assert(!memcmp(out+18,input+28,4));
  if(format==7)assert(out[18]==20 && out[19]==50 && out[20]==80 && out[21]==255);
  if(format==8)assert(out[18]==20 && out[20]==20 && out[21]==50);
  if(format==9)assert(out[18]==20 && out[20]==20 && out[21]==255);
  if(format==10)assert(out[18]==255 && out[20]==255 && out[21]==20);
  free(out);
 }
 header(input,36,6,2,2,0);input[28]=100;input[29]=80;input[30]=60;input[31]=255;
 emit(input+32,&bit,1,0); /* no mip delta */
 emit(input+32,&bit,1,0);for(unsigned i=0;i<3;i++)emit(input+32,&bit,3,1);
 for(unsigned channel=0;channel<2;channel++){emit(input+32,&bit,1,0);for(unsigned i=0;i<3;i++)emit(input+32,&bit,2,3);}
 emit(input+32,&bit,1,0);for(unsigned i=0;i<3;i++)emit(input+32,&bit,1,1);
 assert(bit==29);assert(cod4_iwi_wavelet_to_tga(input,36,&out,&size));assert(size==34);
 for(unsigned i=0;i<4;i++)assert(!memcmp(out+18+i*4,input+28,4));free(out);
 for(unsigned n=0;n<36;n++){out=(unsigned char*)1;size=1;assert(!cod4_iwi_wavelet_to_tga(input,n,&out,&size));assert(!out && !size);}
 put32(input+12,35);assert(!cod4_iwi_wavelet_to_tga(input,36,&out,&size));
 header(input,32,6,3,1,2);assert(!cod4_iwi_wavelet_to_tga(input,32,&out,&size));
 header(input,32,6,32768,32768,0);assert(!cod4_iwi_wavelet_to_tga(input,32,&out,&size));
 header(input,32,6,1,1,4);assert(!cod4_iwi_wavelet_to_tga(input,32,&out,&size));
 unsigned seed=91;
 for(unsigned iteration=0;iteration<1000;iteration++){
  header(input,36,6,2,2,0);for(unsigned i=28;i<36;i++){seed=seed*1664525u+1013904223u;input[i]=seed>>24;}
  cod4_iwi_wavelet_to_tga(input,36,&out,&size);free(out);
 }
 puts("cod4_iwi_wavelet_test: ok");return 0;
}
