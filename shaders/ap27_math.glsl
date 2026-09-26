uint addc(uint a, uint b, inout uint carry) {
    uint s = a + b;
    carry += uint(s < a);
    return s;
}
uvec2 add64(uvec2 a, uvec2 b) {
    uint lo = a.x + b.x;
    return uvec2(lo, a.y + b.y + uint(lo < a.x));
}
uvec2 sub64(uvec2 a, uvec2 b) {
    uint lo = a.x - b.x;
    return uvec2(lo, a.y - b.y - uint(a.x < b.x));
}
bool lt64(uvec2 a, uvec2 b) {
    return a.y < b.y || (a.y == b.y && a.x < b.x);
}
uvec2 mul32wide(uint a, uint b) {
#ifdef USE_UMUL_EXTENDED
    uint hi, lo;
    umulExtended(a,b,hi,lo);
    return uvec2(lo,hi);
#else
    uint a0 = a & 65535u, a1 = a >> 16;
    uint b0 = b & 65535u, b1 = b >> 16;
    uint p00 = a0 * b0, p01 = a0 * b1;
    uint p10 = a1 * b0, p11 = a1 * b1;
    uint mid = (p00 >> 16) + (p01 & 65535u) + (p10 & 65535u);
    return uvec2((p00 & 65535u) | (mid << 16),
                 p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16));
#endif
}
uvec4 mul64wide(uvec2 a, uvec2 b) {
    uvec2 p00 = mul32wide(a.x,b.x), p01 = mul32wide(a.x,b.y);
    uvec2 p10 = mul32wide(a.y,b.x), p11 = mul32wide(a.y,b.y);
    uint c1=0u, w1=addc(p00.y,p01.x,c1); w1=addc(w1,p10.x,c1);
    uint c2=0u, w2=addc(p01.y,p10.y,c2); w2=addc(w2,p11.x,c2);
    uint c3=0u; w2=addc(w2,c1,c3);
    return uvec4(p00.x,w1,w2,p11.y+c2+c3);
}
uvec2 mul64low(uvec2 a, uvec2 b) {
    uvec2 p00=mul32wide(a.x,b.x);
    return uvec2(p00.x,p00.y+a.x*b.y+a.y*b.x);
}
uvec2 addmod(uvec2 a, uvec2 b, uvec2 n) {
    // a,b<n. Subtract before adding so even a+b >= 2^64 is exact.
    uvec2 nb=sub64(n,b);
    return !lt64(a,nb) ? sub64(a,nb) : add64(a,b);
}
uint inverse32(uint n) {
    uint q=1u;
    for (uint i=0u;i<5u;i++) q *= 2u-n*q;
    return q;
}
uvec2 inverse64(uvec2 n) {
#ifdef INVERSE_FAST
    // Odd n satisfies n*n == 1 mod 8, so n is an initial 3-bit inverse.
    uvec2 q=n;
    for (uint i=0u;i<5u;i++)
#else
    // Newton lift 1 -> 2 -> 4 -> 8 -> 16 -> 32 -> 64 correct bits.
    uvec2 q=uvec2(1u,0u);
    for (uint i=0u;i<6u;i++)
#endif
        q=mul64low(q,sub64(uvec2(2u,0u),mul64low(n,q)));
    return q;
}
uvec2 mont(uvec2 a,uvec2 b,uvec2 n,uvec2 q) {
    uvec4 ab=mul64wide(a,b);
    uvec2 m=mul64low(ab.xy,q);
    uvec4 mn=mul64wide(m,n);
    uvec2 h=ab.zw, mh=mn.zw;
    uvec2 r=sub64(h,mh);
    return lt64(h,mh) ? add64(r,n) : r;
}
uvec2 one_mont(uvec2 n) {
    uvec2 r=uvec2(1u,0u);
    for(uint i=0u;i<64u;i++) r=addmod(r,r,n);
    return r;
}
uint getbit(uvec2 x,uint bit) { return bit<32u ? ((x.x>>bit)&1u) : ((x.y>>(bit-32u))&1u); }
uvec2 shr64(uvec2 x,uint s) {
    if(s==0u) return x;
    if(s<32u) return uvec2((x.x>>s)|(x.y<<(32u-s)),x.y>>s);
    if(s==32u) return uvec2(x.y,0u);
    return uvec2(x.y>>(s-32u),0u);
}
uint trailing64(uvec2 x) { return x.x!=0u ? uint(findLSB(x.x)) : 32u+uint(findLSB(x.y)); }
bool strong_prp(uvec2 n,uvec2 q,uvec2 one) {
    uvec2 nm1=sub64(n,uvec2(1u,0u));
    uint t=trailing64(nm1);
    uvec2 e=shr64(nm1,t);
    uvec2 a=one;
    uvec2 two=addmod(one,one,n);
    bool begun=false;
    for(int i=63;i>=0;i--) {
        uint bit=getbit(e,uint(i));
        if(!begun && bit==0u) continue;
        begun=true;
        a=mont(a,a,n,q);
        if(bit!=0u) a=addmod(a,a,n);
    }
    uvec2 minus_one=sub64(n,one);
    if(all(equal(a,one)) || all(equal(a,minus_one))) return true;
    for(uint s=1u;s<t;s++) {
        a=mont(a,a,n,q);
        if(all(equal(a,minus_one))) return true;
    }
    return false;
}
