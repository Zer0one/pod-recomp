#include "glide_vertex.h"
#include <math.h>
#include <string.h>

typedef struct { int local_rgb,local_alpha,other_rgb,other_alpha,texture; } inputs;
static inputs used(const pod_glide_state *s) {
    unsigned f=s->p[GS_COLOR_FUNC],k=s->p[GS_COLOR_FACTOR];
    inputs u={0};
    u.local_rgb=f==1||f==4||f==6||f==7||f==8||f==9;
    u.local_alpha=f==2||f==5||f==8||f==16;
    u.other_rgb=f>=3&&f<=8;
    if(f>=3) {
        u.local_rgb|=k==1||k==9;
        u.local_alpha|=k==3||k==11;
        u.other_alpha=k==2||k==10;
        u.texture=k==4||k==5||k==12||k==13;
    }
    u.texture|=(u.other_rgb||u.other_alpha)&&s->p[GS_COLOR_OTHER]==1;
    u.texture|=s->p[GS_ALPHA_SOURCE]>=2;
    return u;
}
int pod_glide_uses_texture(const pod_glide_state *s) { return used(s).texture; }
unsigned pod_glide_vertex_decode(const pod_glide_state *s,const float guest[12],pod_glide_vertex *v) {
    inputs u=used(s);
    int rgb=(u.local_rgb&&s->p[GS_COLOR_LOCAL]==0)||(u.other_rgb&&s->p[GS_COLOR_OTHER]==0);
    int alpha=(u.local_alpha&&s->p[GS_COLOR_LOCAL]==0)||(u.other_alpha&&s->p[GS_COLOR_OTHER]==0)||s->p[GS_ALPHA_SOURCE]==1||s->p[GS_ALPHA_SOURCE]==3||s->p[GS_FOG_MODE]==1;
    int z=s->p[GS_DEPTH_MODE]==1||((u.local_rgb||u.local_alpha)&&s->p[GS_COLOR_LOCAL]==2);
    int w=s->p[GS_DEPTH_MODE]==2||u.texture;
    const int mask[12]={1,1,0,rgb,rgb,rgb,z,alpha,w,u.texture,u.texture,0};
    float f[12]={0};
    for(unsigned k=0;k<12;k++)if(mask[k]) { if(!isfinite(guest[k]))return k+1; f[k]=guest[k]; }
    /* No grHints import in POD: TMU0 shares FBI oow; guest[11] is unused. */
    *v=(pod_glide_vertex){f[0],f[1],f[6],f[8],f[3],f[4],f[5],f[7],f[9],f[10],f[8],0};
    return 0;
}
