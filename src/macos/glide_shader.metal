#include <metal_stdlib>
using namespace metal;
struct V { float x,y,ooz,oow,r,g,b,a,sow,tow,q,pad; };
struct S { uint p[GS_PARAMETER_COUNT]; float f[GS_FLOAT_COUNT]; };
static_assert(sizeof(V)==48, "Glide vertex ABI");
static_assert(sizeof(S)==448, "Glide state ABI");
struct O { float4 pos [[position]]; float4 color; float3 uvq; float2 depth; };
vertex O glide_vs(uint id [[vertex_id]],device const V *v [[buffer(0)]],constant S &s [[buffer(1)]]) {
    V a=v[id]; O o;
    o.pos=float4(a.x/float(s.p[GS_WIDTH])*2-1,1-a.y/float(s.p[GS_HEIGHT])*2,0,1);
    o.color=float4(a.r,a.g,a.b,a.a)/255.0;
    o.uvq=float3(a.sow,a.tow,a.q); o.depth=float2(a.ooz,a.oow); return o;
}
float4 unpack(uint c) { return float4((c>>16)&255,(c>>8)&255,c&255,c>>24)/255.0; }
float3 factor(uint f,float4 l,float4 o,float4 t) {
    switch(f) {
      case 0:return float3(0); case 1:return l.rgb; case 2:return float3(o.a);
      case 3:return float3(l.a); case 4:return float3(t.a); case 5:return t.rgb;
      case 8:return float3(1); case 9:return 1-l.rgb; case 10:return float3(1-o.a);
      case 11:return float3(1-l.a); case 12:return float3(1-t.a); default:return 1-t.rgb;
    }
}
// Encode reciprocal W into the SST-1 4-bit exponent / 12-bit mantissa
// depth domain. Interpolation remains Metal float; this does not reproduce
// the fixed-point triangle setup or all Voodoo rasterization edge rules.
float w_depth(float q) {
    if(q<0.0 || q>=1.0)return 0.0;
    if(q<=1.0/65536.0)return 1.0;
    uint exponent=(as_type<uint>(q)>>23)&255u;
    uint band=126u-exponent;
    uint significand=uint(floor(ldexp(q,int(band)+1)*4096.0));
    uint encoded=min(65535u,band*4096u+8192u-significand);
    return float(encoded)/65535.0;
}
struct F { float4 color [[color(0)]]; float depth [[depth(any)]]; };
fragment F glide_fs(O i [[stage_in]],constant S &s [[buffer(0)]],texture2d<float> tex [[texture(0)]],sampler smp [[sampler(0)]]) {
    float2 uv=abs(i.uvq.z)>1e-20 ? i.uvq.xy/i.uvq.z*float2(s.f[4],s.f[5]) : float2(0);
    float4 t=s.p[GS_TEXTURE] ? (s.p[GS_MIPMAP] ? tex.sample(smp,uv) : tex.sample(smp,uv,level(0))) : float4(1);
    if(s.p[GS_TEX_FUNC]==0||s.p[GS_TEX_FUNC]==2)t=float4(0); else if(s.p[GS_TEX_FUNC]==10)t=float4(1);
    if(s.p[GS_TEX_INVERT])t.rgb=1-t.rgb; if(s.p[GS_TEX_ALPHA_INVERT])t.a=1-t.a;
    if(s.p[GS_KEY_MODE] && all(abs(t.rgb-unpack(s.p[GS_KEY_COLOR]).rgb)<float3(0.5/255.0))) discard_fragment();
    float4 cc=float4(s.f[0],s.f[1],s.f[2],s.f[3]);
    float4 l=s.p[GS_COLOR_LOCAL]==0?i.color:(s.p[GS_COLOR_LOCAL]==1?cc:float4(i.depth.x/65535.0));
    float4 o=s.p[GS_COLOR_OTHER]==0?i.color:(s.p[GS_COLOR_OTHER]==1?t:cc);
    float3 k=factor(s.p[GS_COLOR_FACTOR],l,o,t),rgb=float3(0);
    switch(s.p[GS_COLOR_FUNC]) {
      case 0:break; case 1:rgb=l.rgb;break; case 2:rgb=float3(l.a);break;
      case 3:rgb=k*o.rgb;break; case 4:rgb=k*o.rgb+l.rgb;break;
      case 5:rgb=k*o.rgb+l.a;break; case 6:rgb=k*(o.rgb-l.rgb);break;
      case 7:rgb=k*(o.rgb-l.rgb)+l.rgb;break; case 8:rgb=k*(o.rgb-l.rgb)+l.a;break;
      case 9:rgb=(1-k)*l.rgb;break; case 16:rgb=(1-k)*l.a;break;
    }
    if(s.p[GS_COLOR_INVERT])rgb=1-rgb;
    float a=s.p[GS_ALPHA_SOURCE]==0?cc.a:(s.p[GS_ALPHA_SOURCE]==1?i.color.a:(s.p[GS_ALPHA_SOURCE]==2?t.a:t.a*i.color.a));
    // Table/Z fog is rejected by the service until its lookup and blending
    // precision have their own verification. Iterated-alpha fog uses alpha.
    if(s.p[GS_FOG_MODE]==1)rgb=mix(rgb,unpack(s.p[GS_FOG_COLOR]).rgb,clamp(i.color.a,0.0,1.0));
    F out;out.color=clamp(float4(rgb,a),0.0,1.0);
    out.depth=s.p[GS_DEPTH_MODE]==2 ? w_depth(i.depth.y) : clamp(i.depth.x/65535.0,0.0,1.0);
    return out;
}
