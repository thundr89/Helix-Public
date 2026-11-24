/* GPL-2.0-or-later. Allocation-free, authoritative notetrack cursor. */
#include "anim/notes.h"
#include <math.h>
void cod_notes_reset(CodNoteCursor* c,float duration,float rate){c->phase=rate<0?duration:0;c->initial=1;}
int cod_notes_advance(CodNoteCursor* c,const hxSGAnimationNote* notes,int count,float duration,int looped,double seconds,float rate,CodNoteEmit emit,void* user){
    double a,b,first,last,k;int i,events=0,overflow=0;
    if(!c || count<0 || count>256 || (count && !notes) || !isfinite(seconds) || seconds<0 || !isfinite(rate) || !isfinite(duration) || duration<0)return -1;
    a=c->phase;b=a+seconds*rate;
    if(!looped){if(b<0)b=0;if(b>duration)b=duration;}
    if(duration<=0){if(c->initial)for(i=0;i<count;i++){if(emit)emit(user,notes[i].name);events++;}c->initial=0;return events;}
    first=looped?floor(a/duration):0;last=looped?floor(b/duration):0;
    if(fabs(last-first)>64){overflow=1;if(rate<0)last=first-64;else last=first+64;}
    if(rate>=0){
        for(k=first;k<=last;k++)for(i=0;i<count;i++){
            double time=k*duration+notes[i].time*duration;
            if((time>a || (c->initial && time==a)) && time<=b){if(events==1024){overflow=1;goto done;}if(emit)emit(user,notes[i].name);events++;}
        }
    }else{
        for(k=first;k>=last;k--)for(i=count-1;i>=0;i--){
            double time=k*duration+notes[i].time*duration;
            if((time<a || (c->initial && time==a)) && time>=b){if(events==1024){overflow=1;goto done;}if(emit)emit(user,notes[i].name);events++;}
        }
    }
done:
    c->phase=looped?fmod(b,duration):b;if(c->phase<0)c->phase+=duration;c->initial=0;return overflow?-1:events;
}
