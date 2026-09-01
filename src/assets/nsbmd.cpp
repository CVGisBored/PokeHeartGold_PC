#include "assets/nsbmd.hpp"
#include "assets/narc.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {
using Bytes=std::vector<unsigned char>;
uint16_t rd16(const Bytes& b,size_t p){if(p+2>b.size())throw std::runtime_error("u16 out of range");return uint16_t(b[p])|(uint16_t(b[p+1])<<8);}
uint32_t rd32(const Bytes& b,size_t p){if(p+4>b.size())throw std::runtime_error("u32 out of range");return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
int32_t signExtend(uint32_t x,unsigned bits){uint32_t m=1u<<(bits-1);x&=(1u<<bits)-1;return int32_t((x^m)-m);}
std::string name16(const Bytes& b,size_t p){if(p+16>b.size())throw std::runtime_error("name out of range");size_t n=0;while(n<16&&b[p+n])++n;return std::string(reinterpret_cast<const char*>(b.data()+p),n);}
Color rgb555(uint32_t x,float alpha=1.0f){return {float(x&31)/31.0f,float((x>>5)&31)/31.0f,float((x>>10)&31)/31.0f,alpha};}
Color cmul(Color a,Color b){return {a.r*b.r,a.g*b.g,a.b*b.b,a.a*b.a};}

struct RawInfoBlock { std::vector<std::vector<unsigned char>> datum; std::vector<std::string> names; };
RawInfoBlock infoRaw(const Bytes& b,size_t p,size_t datumSize){
    if(p+16>b.size()||b[p]!=0) throw std::runtime_error("invalid info block");
    unsigned count=b[p+1];
    size_t sizePos=p+12+4ull*count;
    if(sizePos+4>b.size()) throw std::runtime_error("info block truncated");
    if(rd16(b,sizePos)!=datumSize) throw std::runtime_error("unexpected info datum size");
    size_t d=p+16+4ull*count;
    if(d+datumSize*count+16ull*count>b.size()) throw std::runtime_error("info block data truncated");
    RawInfoBlock out;
    for(unsigned i=0;i<count;i++)
        out.datum.emplace_back(b.begin()+ptrdiff_t(d+datumSize*i),b.begin()+ptrdiff_t(d+datumSize*(i+1)));
    size_t np=d+datumSize*count;
    for(unsigned i=0;i<count;i++) out.names.push_back(name16(b,np+16ull*i));
    return out;
}
struct InfoBlock { std::vector<uint32_t> datum; std::vector<std::string> names; };
InfoBlock info32(const Bytes& b,size_t p){auto raw=infoRaw(b,p,4);InfoBlock out;out.names=std::move(raw.names);for(auto const& d:raw.datum)out.datum.push_back(uint32_t(d[0])|(uint32_t(d[1])<<8)|(uint32_t(d[2])<<16)|(uint32_t(d[3])<<24));return out;}

struct Material {
    std::string name;
    Color diffuse{1,1,1,1};
    Color ambient{1,1,1,1};
    Color emission{0,0,0,1};
    bool vertexDefault=false;
    bool lightingEnabled=false;
    uint32_t polygonAttr=0;
    uint32_t texImageParams=0;
    int textureIndex=-1;
    int paletteIndex=-1;
    std::string textureName;
    std::string paletteName;
};

// Nitro model materials use two pairing NameLists: one maps texture names to
// material indices and the other maps palette names to material indices.
// The MaterialIdxList offsets are relative to the MaterialList base in HG/SS.
std::vector<std::string> readPairingNames(const Bytes& b,size_t materialBase,size_t relField,size_t materialCount){
    std::vector<std::string> mapping(materialCount);
    if(materialBase+relField+2>b.size()) return mapping;
    uint16_t rel=rd16(b,materialBase+relField);
    if(rel==0) return mapping;
    size_t p=materialBase+rel;
    try{
        if(p+16>b.size()||b[p]!=0) return mapping;
        unsigned count=b[p+1];
        size_t sizePos=p+12+4ull*count;
        if(sizePos+4>b.size()||rd16(b,sizePos)!=4) return mapping;
        size_t data=p+16+4ull*count;
        size_t namePos=data+4ull*count;
        if(namePos+16ull*count>b.size()) return mapping;
        for(unsigned i=0;i<count;i++){
            size_t d=data+4ull*i;uint16_t listRel=rd16(b,d);unsigned n=b[d+2];
            size_t list=materialBase+listRel;if(list+n>b.size())continue;
            std::string pairedName=name16(b,namePos+16ull*i);
            for(unsigned j=0;j<n;j++){unsigned mi=b[list+j];if(mi<mapping.size())mapping[mi]=pairedName;}
        }
    }catch(...){ }
    return mapping;
}
std::vector<Material> readMaterials(const Bytes& b,size_t modelBase,size_t off,const std::vector<NsbmdTexture>& textures,const std::vector<std::string>& paletteNames){
    size_t base=modelBase+off;if(base+4>b.size())throw std::runtime_error("materials header out of range");
    auto ib=info32(b,base+4);
    auto texPair=readPairingNames(b,base,0,ib.datum.size());
    auto palPair=readPairingNames(b,base,2,ib.datum.size());
    std::vector<Material> out;out.reserve(ib.datum.size());
    for(size_t i=0;i<ib.datum.size();i++){
        uint32_t rel=ib.datum[i];size_t p=base+rel;if(p+24>b.size())throw std::runtime_error("material out of range");
        uint32_t difamb=rd32(b,p+4);uint32_t speemi=rd32(b,p+8);uint32_t poly=rd32(b,p+12);uint32_t texParams=rd32(b,p+20);float a=float((poly>>16)&31)/31.0f;
        Material m;
        if(i<ib.names.size())m.name=ib.names[i];
        m.diffuse=rgb555(difamb&0x7fff,a);
        m.ambient=rgb555((difamb>>16)&0x7fff,1.0f);
        m.emission=rgb555((speemi>>16)&0x7fff,1.0f);
        m.vertexDefault=(difamb&(1u<<15))!=0;
        m.polygonAttr=poly;
        m.texImageParams=texParams;
        m.lightingEnabled=(poly&0xFu)!=0;
        if(i<texPair.size())m.textureName=texPair[i];
        if(i<palPair.size())m.paletteName=palPair[i];
        for(size_t ti=0;ti<textures.size();ti++)if(textures[ti].name==m.textureName){m.textureIndex=int(ti);break;}
        for(size_t pi=0;pi<paletteNames.size();pi++)if(paletteNames[pi]==m.paletteName){m.paletteIndex=int(pi);break;}
        out.push_back(std::move(m));
    }
    return out;
}

struct Piece { std::string name; std::vector<unsigned char> commands; };
std::vector<Piece> readPieces(const Bytes& b,size_t modelBase,size_t off){
    size_t base=modelBase+off;auto ib=info32(b,base);std::vector<Piece> out;out.reserve(ib.datum.size());
    for(size_t i=0;i<ib.datum.size();i++){size_t p=base+ib.datum[i];if(p+16>b.size())throw std::runtime_error("piece header out of range");uint32_t cmdOff=rd32(b,p+8),cmdLen=rd32(b,p+12);if(p+cmdOff+cmdLen>b.size())throw std::runtime_error("piece command stream out of range");Piece pc;pc.name=ib.names[i];pc.commands.assign(b.begin()+ptrdiff_t(p+cmdOff),b.begin()+ptrdiff_t(p+cmdOff+cmdLen));out.push_back(std::move(pc));}return out;
}


struct Mat4f {
    float m[16]{};
    static Mat4f identity(){ Mat4f a{}; a.m[0]=a.m[5]=a.m[10]=a.m[15]=1.0f; return a; }
};
Mat4f mmul(const Mat4f& a,const Mat4f& b){
    Mat4f r{};
    for(int row=0;row<4;row++)for(int col=0;col<4;col++)
        for(int k=0;k<4;k++)r.m[row*4+col]+=a.m[row*4+k]*b.m[k*4+col];
    return r;
}
Mat4f maddScaled(Mat4f a,const Mat4f& b,float w){for(int i=0;i<16;i++)a.m[i]+=b.m[i]*w;return a;}
Mat4f mscale(float x,float y,float z){auto r=Mat4f::identity();r.m[0]=x;r.m[5]=y;r.m[10]=z;return r;}
Mat4f mtranslate(float x,float y,float z){auto r=Mat4f::identity();r.m[3]=x;r.m[7]=y;r.m[11]=z;return r;}
Vec3f transformPoint(const Mat4f& m,Vec3f v){
    float x=m.m[0]*v.x+m.m[1]*v.y+m.m[2]*v.z+m.m[3];
    float y=m.m[4]*v.x+m.m[5]*v.y+m.m[6]*v.z+m.m[7];
    float z=m.m[8]*v.x+m.m[9]*v.y+m.m[10]*v.z+m.m[11];
    float w=m.m[12]*v.x+m.m[13]*v.y+m.m[14]*v.z+m.m[15];
    if(std::fabs(w)>1e-8f&&std::fabs(w-1.0f)>1e-6f){x/=w;y/=w;z/=w;}
    return {x,y,z};
}
Vec3f transformNormal(const Mat4f& m,Vec3f v){
    Vec3f o{m.m[0]*v.x+m.m[1]*v.y+m.m[2]*v.z,
            m.m[4]*v.x+m.m[5]*v.y+m.m[6]*v.z,
            m.m[8]*v.x+m.m[9]*v.y+m.m[10]*v.z};
    float n=std::sqrt(o.x*o.x+o.y*o.y+o.z*o.z);if(n>1e-8f){o.x/=n;o.y/=n;o.z/=n;}return o;
}
NsbmdMatrix toPublic(const Mat4f& m){NsbmdMatrix o{};for(int i=0;i<16;i++)o[std::size_t(i)]=m.m[i];return o;}
Mat4f fromPublic(const NsbmdMatrix& m){Mat4f o{};for(int i=0;i<16;i++)o.m[i]=m[std::size_t(i)];return o;}
Mat4f inverseAffine(const Mat4f& m){
    // Invert the upper 3x3 and translation; Nitro joint matrices are affine.
    const float a=m.m[0],b=m.m[1],c=m.m[2],d=m.m[4],e=m.m[5],f=m.m[6],g=m.m[8],h=m.m[9],i=m.m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(std::fabs(det)<1e-9f)return Mat4f::identity();
    const float q=1.0f/det;Mat4f r=Mat4f::identity();
    r.m[0]=(e*i-f*h)*q;r.m[1]=(c*h-b*i)*q;r.m[2]=(b*f-c*e)*q;
    r.m[4]=(f*g-d*i)*q;r.m[5]=(a*i-c*g)*q;r.m[6]=(c*d-a*f)*q;
    r.m[8]=(d*h-e*g)*q;r.m[9]=(b*g-a*h)*q;r.m[10]=(a*e-b*d)*q;
    const float tx=m.m[3],ty=m.m[7],tz=m.m[11];
    r.m[3]=-(r.m[0]*tx+r.m[1]*ty+r.m[2]*tz);
    r.m[7]=-(r.m[4]*tx+r.m[5]*ty+r.m[6]*tz);
    r.m[11]=-(r.m[8]*tx+r.m[9]*ty+r.m[10]*tz);
    return r;
}
float fx16s(uint16_t x){return float(int16_t(x))/4096.0f;}
float fx32s(uint32_t x){return float(int32_t(x))/4096.0f;}
Mat4f pivotMatrix(unsigned select,unsigned neg,float a,float b){
    float o=(neg&1)?-1.0f:1.0f,c=(neg&2)?-b:b,d=(neg&4)?-a:a;
    float q[9]{};
    switch(select){
      case 0:{float t[9]={o,0,0,0,a,b,0,c,d};std::copy(t,t+9,q);break;}
      case 1:{float t[9]={0,o,0,a,0,b,c,0,d};std::copy(t,t+9,q);break;}
      case 2:{float t[9]={0,0,o,a,b,0,c,d,0};std::copy(t,t+9,q);break;}
      case 3:{float t[9]={0,a,b,o,0,0,0,c,d};std::copy(t,t+9,q);break;}
      case 4:{float t[9]={a,0,b,0,o,0,c,0,d};std::copy(t,t+9,q);break;}
      case 5:{float t[9]={a,b,0,0,0,o,c,d,0};std::copy(t,t+9,q);break;}
      case 6:{float t[9]={0,a,b,0,c,d,o,0,0};std::copy(t,t+9,q);break;}
      case 7:{float t[9]={a,0,b,c,0,d,0,o,0};std::copy(t,t+9,q);break;}
      case 8:{float t[9]={a,b,0,c,d,0,0,0,o};std::copy(t,t+9,q);break;}
      default:q[0]=-a;break;
    }
    auto r=Mat4f::identity();for(int row=0;row<3;row++)for(int col=0;col<3;col++)r.m[row*4+col]=q[col*3+row];return r;
}
std::vector<Mat4f> readObjectMatrices(const Bytes& b,size_t modelBase){
    std::vector<Mat4f> out;
    try{
        auto ib=info32(b,modelBase+64);out.reserve(ib.datum.size());
        for(uint32_t rel:ib.datum){
            size_t p=modelBase+64+rel;if(p+4>b.size())throw std::runtime_error("object matrix truncated");
            uint16_t flags=rd16(b,p),m0=rd16(b,p+2);p+=4;
            bool t=(flags&1)!=0,r=(flags&2)!=0,s=(flags&4)!=0,pivot=(flags&8)!=0;
            Vec3f tr{0,0,0},sc{1,1,1};Mat4f rot=Mat4f::identity();
            if(!t){tr={fx32s(rd32(b,p)),fx32s(rd32(b,p+4)),fx32s(rd32(b,p+8))};p+=12;}
            if(pivot){float a=fx16s(rd16(b,p)),bb=fx16s(rd16(b,p+2));p+=4;rot=pivotMatrix((flags>>4)&15,(flags>>8)&15,a,bb);}
            else if(!r){if(p+16>b.size())throw std::runtime_error("object rotation truncated");uint16_t q[8];for(int i=0;i<8;i++)q[i]=rd16(b,p+2*i);p+=16;float z[9]={fx16s(m0),fx16s(q[0]),fx16s(q[1]),fx16s(q[2]),fx16s(q[3]),fx16s(q[4]),fx16s(q[5]),fx16s(q[6]),fx16s(q[7])};for(int row=0;row<3;row++)for(int col=0;col<3;col++)rot.m[row*4+col]=z[col*3+row];}
            if(!s){sc={fx32s(rd32(b,p)),fx32s(rd32(b,p+4)),fx32s(rd32(b,p+8))};p+=12;}
            out.push_back(mmul(mtranslate(tr.x,tr.y,tr.z),mmul(rot,mscale(sc.x,sc.y,sc.z))));
        }
    }catch(...){out.clear();}
    return out;
}
std::vector<Mat4f> readInvBinds(const Bytes& b,size_t modelBase,size_t off,size_t count){
    std::vector<Mat4f> out;if(!off)return out;size_t p=modelBase+off;
    for(size_t n=0;n<count&&p+84<=b.size();n++,p+=84){auto m=Mat4f::identity();float v[12];for(int i=0;i<12;i++)v[i]=fx32s(rd32(b,p+4*i));
        // Nitro stores the 4x3 transform in column-major form. Convert to our row-major matrix.
        m.m[0]=v[0];m.m[1]=v[3];m.m[2]=v[6];m.m[3]=v[9];
        m.m[4]=v[1];m.m[5]=v[4];m.m[6]=v[7];m.m[7]=v[10];
        m.m[8]=v[2];m.m[9]=v[5];m.m[10]=v[8];m.m[11]=v[11];out.push_back(m);}
    return out;
}
struct DrawState {int piece=0,material=0,joint=-1;Mat4f current=Mat4f::identity();std::array<Mat4f,32> stack{};};
std::vector<DrawState> renderDraws(const Bytes& b,size_t base,size_t off,size_t pieceCount,const std::vector<Mat4f>& objects,const std::vector<Mat4f>& invBinds,float upScale,float downScale,std::vector<Mat4f>* jointWorld=nullptr,std::vector<int>* jointParents=nullptr,bool applySbcScale=false){
    std::vector<DrawState> out;size_t p=base+off;int mat=0,activeJoint=-1;size_t guard=0;Mat4f cur=Mat4f::identity();std::array<Mat4f,32> stack{};for(auto& x:stack)x=Mat4f::identity();
    if(jointWorld){jointWorld->assign(objects.size(),Mat4f::identity());}if(jointParents){jointParents->assign(objects.size(),-1);}
    auto params=[&](uint8_t op)->size_t{switch(op){case 0x00:case 0x01:case 0x0b:case 0x2b:case 0x40:case 0x80:return 0;case 0x03:case 0x04:case 0x05:case 0x07:case 0x08:case 0x24:case 0x44:return 1;case 0x02:case 0x0c:case 0x0d:case 0x47:return 2;case 0x06:return 3;case 0x26:case 0x46:return 4;case 0x66:return 5;default:return size_t(-1);}};
    while(p<b.size()&&guard++<100000){uint8_t op=b[p++];if(op==0x01)break;
        if(op==0x09){if(p+2>b.size())throw std::runtime_error("skin render command truncated");unsigned dst=b[p],count=b[p+1];size_t n=2+3ull*count;if(p+n>b.size())throw std::runtime_error("skin render command params truncated");Mat4f sum{};for(unsigned i=0;i<count;i++){unsigned sp=b[p+2+3*i],ib=b[p+3+3*i];float w=float(b[p+4+3*i])/256.0f;if(sp<stack.size()&&ib<invBinds.size())sum=maddScaled(sum,mmul(stack[sp],invBinds[ib]),w);}if(dst<stack.size())stack[dst]=sum;cur=sum;p+=n;continue;}
        size_t n=params(op);if(n==size_t(-1))throw std::runtime_error("unknown model render opcode");if(p+n>b.size())throw std::runtime_error("model render command truncated");const unsigned char* q=b.data()+p;
        if(op==0x03&&q[0]<stack.size())cur=stack[q[0]];
        else if(op==0x04||op==0x24||op==0x44)mat=q[0];
        else if(op==0x05&&q[0]<pieceCount){DrawState d;d.piece=q[0];d.material=mat;d.joint=activeJoint;d.current=cur;d.stack=stack;out.push_back(std::move(d));}
        else if(op==0x06||op==0x26||op==0x46||op==0x66){unsigned obj=q[0],parent=q[1];std::optional<unsigned> store,load;if(op==0x26)store=q[3];else if(op==0x46)load=q[3];else if(op==0x66){store=q[3];load=q[4];}if(load&&*load<stack.size())cur=stack[*load];if(obj<objects.size()){cur=mmul(cur,objects[obj]);activeJoint=int(obj);if(jointWorld&&obj<jointWorld->size())(*jointWorld)[obj]=cur;if(jointParents&&obj<jointParents->size())(*jointParents)[obj]=(parent==obj)?-1:int(parent);}if(store&&*store<stack.size())stack[*store]=cur;}
        // Preserve the v0.45 scale path for ordinary HG/SS field/room models.
        // The turbine/anemometer is the one confirmed model whose joint translation
        // must remain outside POSSCALE; applying this globally changed world scale,
        // lighting and several door props. The parser opts this in only for that model.
        else if(op==0x0b){if(applySbcScale)cur=mmul(cur,mscale(upScale,upScale,upScale));}
        else if(op==0x2b){if(applySbcScale)cur=mmul(cur,mscale(downScale,downScale,downScale));}
        p+=n;
    }
    if(out.empty())for(size_t i=0;i<pieceCount;i++){DrawState d;d.piece=int(i);d.material=0;d.current=Mat4f::identity();d.stack=stack;out.push_back(std::move(d));}
    return out;
}

struct VState { Vec3f pos{}; Vec3f normal{0,1,0}; Vec2f uv{}; Color color{1,1,1,1}; Mat4f matrix=Mat4f::identity(); };
std::vector<NsbmdTriangle> decodePiece(const Piece& pc,const Material& material,int materialIndex,const DrawState& draw){
    const auto& b=pc.commands;size_t p=0;std::array<uint8_t,4> fifo{};int fi=4;VState st;st.matrix=draw.current;int prim=-1;std::vector<NsbmdVertex> pv;std::vector<NsbmdTriangle> tris;
    // Several HG/SS room props (plants, tables, chairs and shelves) deliberately
    // use black diffuse + white ambient on an unlit, textured material. The DS
    // field renderer treats those textures as the authored color source. The
    // native decoder previously multiplied the texture by black twice, turning
    // otherwise-valid furniture textures completely black.
    const auto lum=[](Color c){return (c.r+c.g+c.b)/3.0f;};
    const bool textureIsColorSource=material.textureIndex>=0&&!material.lightingEnabled&&
        lum(material.diffuse)<0.06f&&lum(material.ambient)>0.75f;
    auto nparams=[](uint8_t op)->int{switch(op){case 0x00:return 0;case 0x14:return 1;case 0x1b:return 3;case 0x20:case 0x21:case 0x22:return 1;case 0x23:return 2;case 0x24:case 0x25:case 0x26:case 0x27:case 0x28:return 1;case 0x40:return 1;case 0x41:return 0;default:return -1;}};
    auto emitTri=[&](size_t a,size_t c,size_t d){if(a>=pv.size()||c>=pv.size()||d>=pv.size())return;NsbmdTriangle t;
        t.a=pv[a];t.b=pv[c];t.c=pv[d];
        t.materialColor=textureIsColorSource?Color{1,1,1,material.diffuse.a}:material.diffuse;t.materialIndex=materialIndex;t.textureIndex=material.textureIndex;t.paletteIndex=material.paletteIndex;
        t.ambientColor=material.ambient;t.emissionColor=material.emission;t.polygonAttr=material.polygonAttr;t.texImageParams=material.texImageParams;t.lightingEnabled=material.lightingEnabled;t.jointIndex=draw.joint;
        {
            Vec3f e1{t.b.position.x-t.a.position.x,t.b.position.y-t.a.position.y,t.b.position.z-t.a.position.z};
            Vec3f e2{t.c.position.x-t.a.position.x,t.c.position.y-t.a.position.y,t.c.position.z-t.a.position.z};
            Vec3f n{e1.y*e2.z-e1.z*e2.y,e1.z*e2.x-e1.x*e2.z,e1.x*e2.y-e1.y*e2.x};
            float nl=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z),shade=1.0f;
            if(t.lightingEnabled&&nl>0.0001f){n.x/=nl;n.y/=nl;n.z/=nl;const float nd=std::max(0.0f,0.24f*n.x+0.91f*n.y+0.33f*n.z);shade=0.88f+0.12f*nd;}
            Color vc{(t.a.color.r+t.b.color.r+t.c.color.r)/3.0f,(t.a.color.g+t.b.color.g+t.c.color.g)/3.0f,(t.a.color.b+t.b.color.b+t.c.color.b)/3.0f,1};
            Color base{t.materialColor.r*vc.r,t.materialColor.g*vc.g,t.materialColor.b*vc.b,t.materialColor.a};
            if(t.lightingEnabled){
                base.r=std::clamp(base.r*shade+t.ambientColor.r*0.07f+t.emissionColor.r,0.0f,1.0f);
                base.g=std::clamp(base.g*shade+t.ambientColor.g*0.07f+t.emissionColor.g,0.0f,1.0f);
                base.b=std::clamp(base.b*shade+t.ambientColor.b*0.07f+t.emissionColor.b,0.0f,1.0f);
            }
            t.rasterBaseColor=base;
        }
        tris.push_back(t);};
    auto flush=[&](){if(prim==0){for(size_t i=0;i+2<pv.size();i+=3)emitTri(i,i+1,i+2);}else if(prim==1){for(size_t i=0;i+3<pv.size();i+=4){emitTri(i,i+1,i+2);emitTri(i,i+2,i+3);}}else if(prim==2){for(size_t i=2;i<pv.size();i++){if(i&1)emitTri(i-1,i-2,i);else emitTri(i-2,i-1,i);}}else if(prim==3){for(size_t i=2;i+1<pv.size();i+=2){emitTri(i-2,i-1,i+1);emitTri(i-2,i+1,i);}}pv.clear();};
    auto addVertex=[&](){NsbmdVertex v;v.position=transformPoint(st.matrix,st.pos);v.normal=transformNormal(st.matrix,st.normal);v.uv=st.uv;v.color=(material.vertexDefault&&!textureIsColorSource)?cmul(st.color,material.diffuse):st.color;pv.push_back(v);};
    while(p<b.size()||fi<4){if(fi>=4){if(p+4>b.size())throw std::runtime_error("gpu opcode packet truncated");for(int i=0;i<4;i++)fifo[i]=b[p+i];p+=4;fi=0;}uint8_t op=fifo[fi++];int np=nparams(op);if(np<0)throw std::runtime_error("unsupported GPU opcode");if(p+4ull*np>b.size())throw std::runtime_error("gpu params truncated");uint32_t q0=np?rd32(b,p):0,q1=np>1?rd32(b,p+4):0,q2=np>2?rd32(b,p+8):0;p+=4ull*np;
        switch(op){
            case 0x14:if((q0&31)<draw.stack.size())st.matrix=draw.stack[q0&31];break;
            case 0x1b:st.matrix=mmul(st.matrix,mscale(fx32s(q0),fx32s(q1),fx32s(q2)));break;
            case 0x20:st.color=rgb555(q0&0x7fff,1);break;
            case 0x21:st.normal={signExtend(q0&1023,10)/512.0f,signExtend((q0>>10)&1023,10)/512.0f,signExtend((q0>>20)&1023,10)/512.0f};break;
            case 0x22:st.uv={signExtend(q0&0xffff,16)/16.0f,signExtend((q0>>16)&0xffff,16)/16.0f};break;
            case 0x23:st.pos={signExtend(q0&0xffff,16)/4096.0f,signExtend((q0>>16)&0xffff,16)/4096.0f,signExtend(q1&0xffff,16)/4096.0f};addVertex();break;
            case 0x24:st.pos={signExtend(q0&1023,10)/64.0f,signExtend((q0>>10)&1023,10)/64.0f,signExtend((q0>>20)&1023,10)/64.0f};addVertex();break;
            case 0x25:st.pos.x=signExtend(q0&0xffff,16)/4096.0f;st.pos.y=signExtend((q0>>16)&0xffff,16)/4096.0f;addVertex();break;
            case 0x26:st.pos.x=signExtend(q0&0xffff,16)/4096.0f;st.pos.z=signExtend((q0>>16)&0xffff,16)/4096.0f;addVertex();break;
            case 0x27:st.pos.y=signExtend(q0&0xffff,16)/4096.0f;st.pos.z=signExtend((q0>>16)&0xffff,16)/4096.0f;addVertex();break;
            case 0x28:st.pos.x+=signExtend(q0&1023,10)/4096.0f;st.pos.y+=signExtend((q0>>10)&1023,10)/4096.0f;st.pos.z+=signExtend((q0>>20)&1023,10)/4096.0f;addVertex();break;
            case 0x40:if(prim>=0)flush();prim=int(q0&3);break;
            case 0x41:if(prim>=0){flush();prim=-1;}break;
            default:break;
        }
    }if(prim>=0)flush();return tris;
}


static unsigned char x5to8(unsigned x){return static_cast<unsigned char>((x<<3)|(x>>2));}
static std::array<unsigned char,4> rgba555(uint16_t c,unsigned a5=31){return {x5to8(c&31),x5to8((c>>5)&31),x5to8((c>>10)&31),x5to8(a5&31)};}
static std::array<unsigned char,4> avg2(std::array<unsigned char,4>a,std::array<unsigned char,4>b){return {static_cast<unsigned char>((unsigned(a[0])+b[0])/2),static_cast<unsigned char>((unsigned(a[1])+b[1])/2),static_cast<unsigned char>((unsigned(a[2])+b[2])/2),static_cast<unsigned char>((unsigned(a[3])+b[3])/2)};}
static std::array<unsigned char,4> avg358(std::array<unsigned char,4>a,std::array<unsigned char,4>b){return {static_cast<unsigned char>((3u*a[0]+5u*b[0])/8),static_cast<unsigned char>((3u*a[1]+5u*b[1])/8),static_cast<unsigned char>((3u*a[2]+5u*b[2])/8),static_cast<unsigned char>((3u*a[3]+5u*b[3])/8)};}
static std::vector<NsbmdTexture> decodeTex0(const Bytes& b,size_t o,size_t& paletteCount,std::vector<std::string>& paletteNames){
    std::vector<NsbmdTexture> out;if(o+60>b.size()||std::memcmp(b.data()+o,"TEX0",4))return out;
    uint16_t texInfo=rd16(b,o+14);uint32_t texBlock=rd32(b,o+20),comp1=rd32(b,o+36),comp2=rd32(b,o+40),palInfo=rd32(b,o+52),palBlock=rd32(b,o+56);
    auto ti=infoRaw(b,o+texInfo,8);auto pi=infoRaw(b,o+palInfo,4);paletteCount=pi.datum.size();paletteNames=pi.names;
    struct Pal{uint32_t off=0;};std::vector<Pal> pals;for(auto const& d:pi.datum){uint16_t q=uint16_t(d[0])|(uint16_t(d[1])<<8);pals.push_back({uint32_t(q)<<3});}
    auto palColor=[&](size_t palIdx,size_t entry){if(palIdx>=pals.size())return std::array<unsigned char,4>{255,0,255,255};size_t q=o+palBlock+pals[palIdx].off+entry*2;if(q+2>b.size())return std::array<unsigned char,4>{255,0,255,255};return rgba555(uint16_t(b[q])|(uint16_t(b[q+1])<<8));};
    static const int bpp[8]={0,8,2,4,8,2,8,16};
    for(size_t i=0;i<ti.datum.size();i++){
        auto const& d=ti.datum[i];uint32_t params=uint32_t(d[0])|(uint32_t(d[1])<<8)|(uint32_t(d[2])<<16)|(uint32_t(d[3])<<24);
        unsigned w=8u<<((params>>20)&7),h=8u<<((params>>23)&7),fmt=(params>>26)&7;bool c0=(params&(1u<<29))!=0;size_t off=(params&0xffffu)<<3;size_t pix=size_t(w)*h;
        if(fmt==0||fmt>7)continue;
        size_t nbytes=pix*bpp[fmt]/8;
        size_t dataBase=o+(fmt==5?comp1:texBlock)+off;if(dataBase+nbytes>b.size())throw std::runtime_error("texture data out of range");
        auto decodeForPalette=[&](size_t palIdx){
            std::vector<unsigned char> rgba;rgba.reserve(pix*4);auto put=[&](std::array<unsigned char,4> c){rgba.insert(rgba.end(),c.begin(),c.end());};
            if(fmt==1){for(size_t n=0;n<pix;n++){uint8_t x=b[dataBase+n];auto c=palColor(palIdx,x&31);unsigned a3=x>>5;unsigned a5=(a3<<2)|(a3>>1);c[3]=x5to8(a5);put(c);}}
            else if(fmt==2){for(size_t n=0;n<nbytes;n++){uint8_t x=b[dataBase+n];for(int sh=0;sh<8;sh+=2){unsigned ix=(x>>sh)&3;auto c=palColor(palIdx,ix);if(ix==0&&c0)c[3]=0;put(c);}}}
            else if(fmt==3){for(size_t n=0;n<nbytes;n++){uint8_t x=b[dataBase+n];for(int sh:{0,4}){unsigned ix=(x>>sh)&15;auto c=palColor(palIdx,ix);if(ix==0&&c0)c[3]=0;put(c);}}}
            else if(fmt==4){for(size_t n=0;n<pix;n++){unsigned ix=b[dataBase+n];auto c=palColor(palIdx,ix);if(ix==0&&c0)c[3]=0;put(c);}}
            else if(fmt==5){size_t data2=o+comp2+off/2;size_t blocks=pix/16;if(data2+blocks*2>b.size())throw std::runtime_error("compressed texture aux out of range");for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){size_t bi=(w/4)*(y/4)+(x/4);uint32_t blk=rd32(b,dataBase+bi*4);uint16_t ex=rd16(b,data2+bi*2);unsigned texel=(blk>>(2*(4*(y%4)+(x%4))))&3;unsigned mode=ex>>14;size_t pa=(ex&0x3fff)<<1;auto C=[&](size_t n){return palColor(palIdx,pa+n);};std::array<unsigned char,4> c{};if(mode==0){if(texel<3)c=C(texel);else c={0,0,0,0};}else if(mode==1){if(texel<2)c=C(texel);else if(texel==2)c=avg2(C(0),C(1));else c={0,0,0,0};}else if(mode==2)c=C(texel);else{if(texel<2)c=C(texel);else if(texel==2)c=avg358(C(1),C(0));else c=avg358(C(0),C(1));}put(c);}}
            else if(fmt==6){for(size_t n=0;n<pix;n++){uint8_t x=b[dataBase+n];auto c=palColor(palIdx,x&7);c[3]=x5to8(x>>3);put(c);}}
            else if(fmt==7){for(size_t n=0;n<pix;n++){uint16_t q=rd16(b,dataBase+n*2);put(rgba555(q,(q&0x8000)?31:0));}}
            return rgba;
        };
        NsbmdTexture t;t.name=ti.names[i];t.width=w;t.height=h;t.format=static_cast<uint8_t>(fmt);
        // TEX0 texture indices and palette indices are independent namespaces.
        // The old native decoder used texture index i as the default palette, which
        // happened to turn every follower texture after frame 1 into palette 1. In
        // HG/SS follower models palette 0 is the normal Pokemon palette and palette 1
        // is the shiny palette, so side-facing followers incorrectly became shiny.
        // 3D materials still select their authored palette through paletteVariants;
        // standalone/sprite consumers should default to the first (normal) palette.
        size_t defaultPal=0;
        if(fmt==7){t.rgba=decodeForPalette(0);}
        else if(!pals.empty()){
            t.paletteVariants.reserve(pals.size());
            for(size_t piIdx=0;piIdx<pals.size();piIdx++)t.paletteVariants.push_back(decodeForPalette(piIdx));
            t.rgba=t.paletteVariants[defaultPal];
        } else t.rgba=decodeForPalette(0);
        if(t.rgba.size()==pix*4)out.push_back(std::move(t));
    }
    return out;
}

void bounds(NsbmdModel& m){if(m.triangles.empty()){m.min=m.max={0,0,0};return;}m.min={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};m.max={-m.min.x,-m.min.y,-m.min.z};for(auto const& t:m.triangles)for(auto const* v:{&t.a,&t.b,&t.c}){m.min.x=std::min(m.min.x,v->position.x);m.min.y=std::min(m.min.y,v->position.y);m.min.z=std::min(m.min.z,v->position.z);m.max.x=std::max(m.max.x,v->position.x);m.max.y=std::max(m.max.y,v->position.y);m.max.z=std::max(m.max.z,v->position.z);}}
}

std::vector<unsigned char> read_narc_member(const std::filesystem::path& path,std::size_t index){
    std::ifstream f(path,std::ios::binary);
    if(!f)return {};
    f.seekg(0,std::ios::end);
    const auto endPos=f.tellg();
    if(endPos<16)return {};
    const std::uint64_t fileSize=static_cast<std::uint64_t>(endPos);
    f.seekg(0);
    std::array<unsigned char,16> header{};
    f.read(reinterpret_cast<char*>(header.data()),static_cast<std::streamsize>(header.size()));
    if(!f||std::memcmp(header.data(),"NARC",4)!=0)return {};
    auto le16=[](const unsigned char* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);};
    auto le32=[](const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);};
    const unsigned blocks=le16(header.data()+14);
    std::uint64_t pos=16,fat=0,img=0;
    for(unsigned i=0;i<blocks;i++){
        if(pos+8>fileSize)return {};
        f.seekg(static_cast<std::streamoff>(pos));
        std::array<unsigned char,8> bh{};f.read(reinterpret_cast<char*>(bh.data()),8);if(!f)return {};
        const std::uint32_t sz=le32(bh.data()+4);
        if(sz<8||pos+sz>fileSize)return {};
        if(std::memcmp(bh.data(),"BTAF",4)==0)fat=pos;
        else if(std::memcmp(bh.data(),"GMIF",4)==0)img=pos;
        pos+=sz;
    }
    if(!fat||!img)return {};
    f.seekg(static_cast<std::streamoff>(fat+8));
    std::array<unsigned char,4> countBuf{};f.read(reinterpret_cast<char*>(countBuf.data()),4);if(!f)return {};
    const std::size_t count=le16(countBuf.data());
    if(index>=count)return {};
    const std::uint64_t entryPos=fat+12+std::uint64_t(index)*8;
    if(entryPos+8>fileSize)return {};
    f.seekg(static_cast<std::streamoff>(entryPos));
    std::array<unsigned char,8> ent{};f.read(reinterpret_cast<char*>(ent.data()),8);if(!f)return {};
    const std::uint32_t start=le32(ent.data()),finish=le32(ent.data()+4);
    const std::uint64_t data0=img+8;
    if(finish<start||data0+finish>fileSize)return {};
    Bytes out(static_cast<std::size_t>(finish-start));
    f.seekg(static_cast<std::streamoff>(data0+start));
    if(!out.empty())f.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(out.size()));
    if(!f&& !out.empty())return {};
    return out;
}

NsbmdMember parse_nitro_texture_container(const std::vector<unsigned char>& b){
    NsbmdMember out;
    try{
        if(b.size()<20) throw std::runtime_error("Nitro container too small");
        bool isBmd=!std::memcmp(b.data(),"BMD0",4), isBtx=!std::memcmp(b.data(),"BTX0",4);
        if(!isBmd&&!isBtx) throw std::runtime_error("not BMD0/BTX0");
        unsigned sections=rd16(b,14);
        for(unsigned i=0;i<sections;i++){
            size_t offPos=16+4ull*i;if(offPos+4>b.size())throw std::runtime_error("section table truncated");
            size_t o=rd32(b,offPos);
            if(o+60<=b.size()&&!std::memcmp(b.data()+o,"TEX0",4)){
                out.textures=decodeTex0(b,o,out.paletteCount,out.paletteNames);
                out.textureCount=out.textures.size();
                break;
            }
        }
        if(out.textures.empty()) throw std::runtime_error("container has no decoded TEX0 textures");
        out.valid=true;
    }catch(const std::exception& e){out.error=e.what();}
    return out;
}

NsbmdMember parse_nsbmd(const std::vector<unsigned char>& b){
    NsbmdMember out;
    try {
        if(b.size()<24||std::memcmp(b.data(),"BMD0",4)) throw std::runtime_error("not BMD0");
        unsigned sections=rd16(b,14);
        size_t mdlo=0;
        for(unsigned i=0;i<sections;i++){
            size_t o=rd32(b,16+4ull*i);
            if(o+8<=b.size()&&!std::memcmp(b.data()+o,"MDL0",4)) mdlo=o;
            if(o+60<=b.size()&&!std::memcmp(b.data()+o,"TEX0",4)){
                out.textures=decodeTex0(b,o,out.paletteCount,out.paletteNames);
                out.textureCount=out.textures.size();
            }
        }
        if(!mdlo) throw std::runtime_error("BMD0 has no MDL0 section");
        auto modelList=info32(b,mdlo+8);
        for(size_t mi=0;mi<modelList.datum.size();mi++){
            size_t base=mdlo+modelList.datum[mi];
            if(base+64>b.size()) throw std::runtime_error("model header truncated");
            uint32_t renderOff=rd32(b,base+4),matOff=rd32(b,base+8),pieceOff=rd32(b,base+12),invBindOff=rd32(b,base+16);
            auto mats=readMaterials(b,base,matOff,out.textures,out.paletteNames);
            if(mats.empty()) mats.push_back({});
            auto pieces=readPieces(b,base,pieceOff);
            auto objects=readObjectMatrices(b,base);
            auto invBinds=readInvBinds(b,base,invBindOff,objects.size());
            NsbmdModel model;
            model.name=modelList.names[mi];
            model.sourcePieces=pieces.size();
            // Keep the proven v0.45 world normalization for all ordinary models.
            // a13_anemo (the visible wind-turbine top) is the narrow exception: its
            // mast geometry is POSSCALE'd while the rotor joint translation is not.
            auto fixed20_12=[&](size_t p)->float{return static_cast<int32_t>(rd32(b,p))/4096.0f;};
            model.upScale=fixed20_12(base+28);
            model.downScale=fixed20_12(base+32);
            // v0.46.4: both retail wind-generator models use POSSCALE around the
            // mast geometry while their animated rotor joint translations remain in
            // the unscaled Nitro joint space.  v0.46.1 only exempted a13_anemo,
            // which broke New Bark's wk_sp1 rotor when its BCA was restored.
            const bool separatedJointScale=(model.name=="a13_anemo"||model.name=="wk_sp1");
            model.normalizedScale=separatedJointScale?(1.0f/64.0f):((std::isfinite(model.upScale)&&model.upScale>0.0f)?model.upScale/64.0f:1.0f);
            std::vector<Mat4f> jointWorld;std::vector<int> jointParents;
            auto draws=renderDraws(b,base,renderOff,pieces.size(),objects,invBinds,model.upScale,model.downScale,&jointWorld,&jointParents,separatedJointScale);
            model.jointLocalMatrices.reserve(objects.size());for(auto const& m:objects)model.jointLocalMatrices.push_back(toPublic(m));
            model.jointBindWorldMatrices.reserve(jointWorld.size());for(auto const& m:jointWorld)model.jointBindWorldMatrices.push_back(toPublic(m));
            model.jointParents=std::move(jointParents);
            model.materialNames.reserve(mats.size());model.materialTextureNames.reserve(mats.size());model.materialPaletteNames.reserve(mats.size());
            for(auto const& mm:mats){model.materialNames.push_back(mm.name);model.materialTextureNames.push_back(mm.textureName);model.materialPaletteNames.push_back(mm.paletteName);}

            for(auto const& draw:draws){
                int pi=draw.piece,ma=draw.material;
                if(pi<0||size_t(pi)>=pieces.size()) continue;
                size_t mindex=(ma>=0&&size_t(ma)<mats.size())?size_t(ma):0;
                auto ts=decodePiece(pieces[size_t(pi)],mats[mindex],int(mindex),draw);
                model.sourceVertices += ts.size()*3;
                model.triangles.insert(model.triangles.end(),ts.begin(),ts.end());
            }
            bounds(model);
            out.models.push_back(std::move(model));
        }
        out.valid=true;
    } catch(const std::exception& e) {
        out.error=e.what();
    }
    return out;
}

void bind_nsbmd_external_textures(NsbmdMember& modelMember,const NsbmdMember& textureMember){
    if(!modelMember.valid||!textureMember.valid||textureMember.textures.empty())return;
    modelMember.textures=textureMember.textures;modelMember.textureCount=modelMember.textures.size();
    modelMember.paletteNames=textureMember.paletteNames;modelMember.paletteCount=textureMember.paletteCount;
    for(auto& model:modelMember.models){
        for(auto& tri:model.triangles){
            if(tri.materialIndex<0)continue;
            size_t mi=size_t(tri.materialIndex);
            if(mi<model.materialTextureNames.size()){
                auto const& n=model.materialTextureNames[mi];for(size_t ti=0;ti<modelMember.textures.size();ti++)if(modelMember.textures[ti].name==n){tri.textureIndex=int(ti);break;}
            }
            if(mi<model.materialPaletteNames.size()){
                auto const& n=model.materialPaletteNames[mi];for(size_t pi=0;pi<modelMember.paletteNames.size();pi++)if(modelMember.paletteNames[pi]==n){tri.paletteIndex=int(pi);break;}
            }
            // v0.46 applied the room-prop black-material workaround to every
            // externally textured land material, which flattened/broke field lighting.
            // Restrict it to authored staircase materials; ordinary terrain keeps the
            // exact v0.45 material/lighting result.
            bool stairMaterial=false;
            if(mi<model.materialNames.size()){
                std::string n=model.materialNames[mi];
                for(char& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));
                stairMaterial=n.find("stair")!=std::string::npos||n.find("kaidan")!=std::string::npos;
            }
            const auto lum=[](Color c){return (c.r+c.g+c.b)/3.0f;};
            if(stairMaterial&&tri.textureIndex>=0&&!tri.lightingEnabled&&lum(tri.materialColor)<0.06f&&lum(tri.ambientColor)>0.75f){
                tri.materialColor={1,1,1,tri.materialColor.a};
                if(lum(tri.a.color)<0.06f)tri.a.color={1,1,1,tri.a.color.a};
                if(lum(tri.b.color)<0.06f)tri.b.color={1,1,1,tri.b.color.a};
                if(lum(tri.c.color)<0.06f)tri.c.color={1,1,1,tri.c.color.a};
                Color vc{(tri.a.color.r+tri.b.color.r+tri.c.color.r)/3.0f,(tri.a.color.g+tri.b.color.g+tri.c.color.g)/3.0f,(tri.a.color.b+tri.b.color.b+tri.c.color.b)/3.0f,1};
                tri.rasterBaseColor={vc.r,vc.g,vc.b,tri.materialColor.a};
            }
        }
    }
}


NsbtaAnimation parse_nsbta(const std::vector<unsigned char>& b){
    NsbtaAnimation out;
    try{
        if(b.size()<24||std::memcmp(b.data(),"BTA0",4))throw std::runtime_error("not BTA0");
        const unsigned sections=rd16(b,14);
        size_t srt=0;
        for(unsigned i=0;i<sections;i++){
            const size_t o=rd32(b,16+4ull*i);
            if(o+8<=b.size()&&!std::memcmp(b.data()+o,"SRT0",4)){srt=o;break;}
        }
        if(!srt)throw std::runtime_error("BTA0 has no SRT0 section");
        auto animations=info32(b,srt+8);
        if(animations.datum.empty())throw std::runtime_error("SRT0 has no material animation");
        // HG/SS build-model animation members use one animation per target model.
        // Parse the first animation and all material tracks it contains.
        const size_t base=srt+animations.datum.front();
        if(base+16>b.size())throw std::runtime_error("material animation truncated");
        out.frameCount=rd16(b,base+4);
        auto tracks=infoRaw(b,base+8,40);
        out.tracks.reserve(tracks.datum.size());
        auto readTranslation=[&](const std::vector<unsigned char>& d,size_t channelOffset)->std::vector<float>{
            std::vector<float> values;
            if(channelOffset+8>d.size())return values;
            const std::uint16_t count=std::uint16_t(d[channelOffset])|(std::uint16_t(d[channelOffset+1])<<8);
            const std::uint8_t flags=d[channelOffset+3];
            const std::uint32_t raw=std::uint32_t(d[channelOffset+4])|(std::uint32_t(d[channelOffset+5])<<8)|
                                    (std::uint32_t(d[channelOffset+6])<<16)|(std::uint32_t(d[channelOffset+7])<<24);
            if(flags==0x10){
                const size_t p=base+raw;
                if(p+2ull*count>b.size())throw std::runtime_error("SRT translation samples truncated");
                values.reserve(count);
                for(std::uint16_t i=0;i<count;i++)values.push_back(float(int16_t(rd16(b,p+2ull*i)))/32.0f);
            }else{
                // Constant SRT channels are stored inline as fixed-point values.
                // Translation is expressed in the same texel-coordinate units as
                // NSBMD TEXCOORD. Keep a single value and reuse it for every frame.
                values.push_back(float(static_cast<std::int32_t>(raw))/4096.0f);
            }
            return values;
        };
        for(size_t i=0;i<tracks.datum.size();i++){
            NsbtaMaterialTrack t;
            if(i<tracks.names.size())t.materialName=tracks.names[i];
            t.uTranslation=readTranslation(tracks.datum[i],24);
            t.vTranslation=readTranslation(tracks.datum[i],32);
            out.tracks.push_back(std::move(t));
        }
        out.valid=true;
    }catch(const std::exception& e){out.error=e.what();}
    return out;
}

NsbtaAnimation load_nsbta_from_narc(const std::filesystem::path& path,std::size_t index){
    auto b=read_narc_member(path,index);
    if(b.empty()){NsbtaAnimation o;o.error="NARC member missing";return o;}
    return parse_nsbta(b);
}

NsbtaAnimation load_build_model_nsbta(const std::filesystem::path& animationListNarc,const std::filesystem::path& animationNarc,std::size_t modelIndex){
    auto list=read_narc_member(animationListNarc,modelIndex);
    NsbtaAnimation none;
    if(list.size()<12){none.error="build-model animation list missing";return none;}
    // HG/SS's 24-byte bm_*_anime_list entries store up to four archive member
    // references beginning at byte 8. Different references can be BCA/BTP/BTA;
    // select the first actual BTA0 member instead of assuming an animation type.
    for(size_t p=8;p+4<=list.size()&&p<24;p+=4){
        const std::uint32_t ref=std::uint32_t(list[p])|(std::uint32_t(list[p+1])<<8)|
                                (std::uint32_t(list[p+2])<<16)|(std::uint32_t(list[p+3])<<24);
        if(ref==0xffffffffu)continue;
        auto bytes=read_narc_member(animationNarc,ref);
        if(bytes.size()>=4&&!std::memcmp(bytes.data(),"BTA0",4)){
            auto a=parse_nsbta(bytes);
            if(a.valid)return a;
        }
    }
    none.error="no NSBTA animation for build model";
    return none;
}

Vec2f sample_nsbta_uv(const NsbtaAnimation& animation,const std::string& materialName,double seconds,double framesPerSecond){
    if(!animation.valid||animation.frameCount==0||materialName.empty())return {};
    const std::uint64_t absoluteFrame=std::uint64_t(std::max(0.0,std::floor(seconds*framesPerSecond)));
    const std::size_t frame=std::size_t(absoluteFrame%animation.frameCount);
    for(auto const& track:animation.tracks){
        if(track.materialName!=materialName)continue;
        auto sample=[frame](const std::vector<float>& v){
            if(v.empty())return 0.0f;
            if(v.size()==1)return v.front();
            return v[std::min(frame,v.size()-1)];
        };
        return {sample(track.uTranslation),sample(track.vTranslation)};
    }
    return {};
}


NsbcaAnimation parse_nsbca(const std::vector<unsigned char>& b){
    NsbcaAnimation out;
    try{
        if(b.size()<24||std::memcmp(b.data(),"BCA0",4))throw std::runtime_error("not BCA0");
        const unsigned sections=rd16(b,14);size_t jnt=0;
        for(unsigned i=0;i<sections;i++){const size_t o=rd32(b,16+4ull*i);if(o+8<=b.size()&&!std::memcmp(b.data()+o,"JNT0",4)){jnt=o;break;}}
        if(!jnt)throw std::runtime_error("BCA0 has no JNT0 section");
        auto animations=info32(b,jnt+8);if(animations.datum.empty())throw std::runtime_error("JNT0 has no animation");
        const size_t base=jnt+animations.datum.front();
        if(base+20>b.size()||std::memcmp(b.data()+base,"J\0AC",4))throw std::runtime_error("joint animation header invalid");
        out.frameCount=rd16(b,base+4);const std::uint16_t objectCount=rd16(b,base+6);
        const size_t pivotBase=base+rd32(b,base+12),basisBase=base+rd32(b,base+16);
        if(!out.frameCount||base+20+2ull*objectCount>b.size())throw std::runtime_error("joint animation truncated");
        struct M3{float v[9]{};};
        auto identity3=[](){M3 m{};m.v[0]=m.v[4]=m.v[8]=1.0f;return m;};
        auto fetchRotation=[&](std::uint16_t ref)->M3{
            if(ref&0x8000u){
                const size_t p=pivotBase+size_t(ref&0x7fffu)*6;if(p+6>b.size())throw std::runtime_error("BCA pivot rotation out of range");
                const std::uint16_t sn=rd16(b,p);const float a=fx16s(rd16(b,p+2)),bb=fx16s(rd16(b,p+4));
                Mat4f q=pivotMatrix(sn&15,(sn>>4)&15,a,bb);M3 m{};for(int r=0;r<3;r++)for(int c=0;c<3;c++)m.v[r*3+c]=q.m[r*4+c];return m;
            }
            const size_t p=basisBase+size_t(ref&0x7fffu)*10;if(p+10>b.size())throw std::runtime_error("BCA basis rotation out of range");
            std::uint16_t input[5]={rd16(b,p+8),rd16(b,p),rd16(b,p+2),rd16(b,p+4),rd16(b,p+6)};
            std::uint16_t raw[6]{};for(int i=0;i<5;i++){raw[i]=std::uint16_t(input[i]>>3);raw[5]=std::uint16_t((raw[5]<<3)|(input[i]&7));}
            auto f13=[](std::uint16_t x){return float(signExtend(x,13))/4096.0f;};
            Vec3f a{f13(raw[1]),f13(raw[2]),f13(raw[3])},bb{f13(raw[4]),f13(raw[0]),f13(raw[5])};
            Vec3f c{a.y*bb.z-a.z*bb.y,a.z*bb.x-a.x*bb.z,a.x*bb.y-a.y*bb.x};
            M3 m{}; // columns a,b,c
            m.v[0]=a.x;m.v[3]=a.y;m.v[6]=a.z;m.v[1]=bb.x;m.v[4]=bb.y;m.v[7]=bb.z;m.v[2]=c.x;m.v[5]=c.y;m.v[8]=c.z;return m;
        };
        auto fillFloatCurve=[&](size_t& p,bool constant,float def,bool scale)->std::vector<float>{
            std::vector<float> frames(out.frameCount,def);
            if(constant){
                if(scale){if(p+8>b.size())throw std::runtime_error("BCA scale constant truncated");float v=fx32s(rd32(b,p));p+=8;std::fill(frames.begin(),frames.end(),v);}
                else {if(p+4>b.size())throw std::runtime_error("BCA translation constant truncated");float v=fx32s(rd32(b,p));p+=4;std::fill(frames.begin(),frames.end(),v);}return frames;
            }
            if(p+8>b.size())throw std::runtime_error("BCA curve descriptor truncated");
            const std::uint32_t info=rd32(b,p),off=rd32(b,p+4);
            p+=8;
            const std::uint16_t start=std::uint16_t(info&0xffffu),end=std::uint16_t((info>>16)&0xfffu);const unsigned width=(info>>28)&3u,logRate=(info>>30)&3u;
            if(end<=start)throw std::runtime_error("BCA curve frame range invalid");
            const size_t count=size_t(end-start)>>logRate;
            if(!count)return frames;
            std::vector<float> values;values.reserve(count);size_t q=base+off;
            for(size_t n=0;n<count;n++){
                if(scale){if(width==0){if(q+8>b.size())throw std::runtime_error("BCA scale curve truncated");values.push_back(fx32s(rd32(b,q)));q+=8;}else{if(q+4>b.size())throw std::runtime_error("BCA short scale curve truncated");values.push_back(fx16s(rd16(b,q)));q+=4;}}
                else {if(width==0){if(q+4>b.size())throw std::runtime_error("BCA translation curve truncated");values.push_back(fx32s(rd32(b,q)));q+=4;}else{if(q+2>b.size())throw std::runtime_error("BCA short translation curve truncated");values.push_back(fx16s(rd16(b,q)));q+=2;}}
            }
            for(size_t fr=0;fr<frames.size();fr++){
                if(fr<=start)frames[fr]=values.front();
                else if(fr>=size_t(end-1))frames[fr]=values.back();
                else{float x=float(fr-start)/float((end-1)-start)*float(values.size()-1);size_t lo=size_t(std::floor(x)),hi=std::min(values.size()-1,lo+1);float t=x-float(lo);frames[fr]=values[lo]*(1.0f-t)+values[hi]*t;}
            }return frames;
        };
        auto fillRotCurve=[&](size_t& p,bool constant)->std::vector<M3>{
            std::vector<M3> frames(out.frameCount,identity3());
            if(constant){if(p+4>b.size())throw std::runtime_error("BCA rotation constant truncated");auto m=fetchRotation(rd16(b,p));p+=4;std::fill(frames.begin(),frames.end(),m);return frames;}
            if(p+8>b.size())throw std::runtime_error("BCA rotation curve descriptor truncated");
            const std::uint32_t info=rd32(b,p),off=rd32(b,p+4);
            p+=8;
            const std::uint16_t start=std::uint16_t(info&0xffffu),end=std::uint16_t((info>>16)&0xfffu);
            const unsigned logRate=(info>>30)&3u;
            if(end<=start)throw std::runtime_error("BCA rotation range invalid");
            const size_t count=size_t(end-start)>>logRate;
            if(!count)return frames;
            std::vector<M3> values;values.reserve(count);size_t q=base+off;if(q+count*2>b.size())throw std::runtime_error("BCA rotation samples truncated");for(size_t n=0;n<count;n++)values.push_back(fetchRotation(rd16(b,q+2*n)));
            for(size_t fr=0;fr<frames.size();fr++){
                if(fr<=start)frames[fr]=values.front();
                else if(fr>=size_t(end-1))frames[fr]=values.back();
                else{float x=float(fr-start)/float((end-1)-start)*float(values.size()-1);size_t lo=size_t(std::floor(x)),hi=std::min(values.size()-1,lo+1);float t=x-float(lo);M3 m{};for(int k=0;k<9;k++)m.v[k]=values[lo].v[k]*(1.0f-t)+values[hi].v[k]*t;frames[fr]=m;}
            }return frames;
        };
        out.tracks.reserve(objectCount);
        for(std::uint16_t oi=0;oi<objectCount;oi++){
            const size_t track=base+rd16(b,base+20+2ull*oi);if(track+4>b.size())throw std::runtime_error("BCA object track out of range");size_t p=track;const std::uint16_t flags=rd16(b,p);p+=2;p++;const int target=int(b[p++]);
            NsbcaJointTrack t;t.jointIndex=target;t.localFrames.assign(out.frameCount,toPublic(Mat4f::identity()));
            if(flags&1u){out.tracks.push_back(std::move(t));continue;}
            std::array<std::vector<float>,3> trans,scale;std::vector<M3> rot(out.frameCount,identity3());
            const bool transAnimated=((flags>>1)&3u)==0;if(transAnimated)for(int k=0;k<3;k++)trans[k]=fillFloatCurve(p,(flags&(1u<<(3+k)))!=0,0.0f,false);else for(auto& v:trans)v.assign(out.frameCount,0.0f);
            const bool rotAnimated=((flags>>6)&3u)==0;if(rotAnimated)rot=fillRotCurve(p,(flags&(1u<<8))!=0);
            const bool scaleAnimated=((flags>>9)&3u)==0;if(scaleAnimated)for(int k=0;k<3;k++)scale[k]=fillFloatCurve(p,(flags&(1u<<(11+k)))!=0,1.0f,true);else for(auto& v:scale)v.assign(out.frameCount,1.0f);
            for(size_t fr=0;fr<out.frameCount;fr++){
                Mat4f m=Mat4f::identity();for(int r=0;r<3;r++)for(int c=0;c<3;c++)m.m[r*4+c]=rot[fr].v[r*3+c];
                m=mmul(m,mscale(scale[0][fr],scale[1][fr],scale[2][fr]));m.m[3]=trans[0][fr];m.m[7]=trans[1][fr];m.m[11]=trans[2][fr];t.localFrames[fr]=toPublic(m);
            }out.tracks.push_back(std::move(t));
        }
        out.valid=true;
    }catch(const std::exception& e){out.error=e.what();}
    return out;
}

NsbcaAnimation load_nsbca_from_narc(const std::filesystem::path& path,std::size_t index){auto b=read_narc_member(path,index);if(b.empty()){NsbcaAnimation o;o.error="NARC member missing";return o;}return parse_nsbca(b);}
NsbcaAnimation load_build_model_nsbca(const std::filesystem::path& animationListNarc,const std::filesystem::path& animationNarc,std::size_t modelIndex){
    auto list=read_narc_member(animationListNarc,modelIndex);NsbcaAnimation none;if(list.size()<12){none.error="build-model animation list missing";return none;}
    for(size_t p=8;p+4<=list.size()&&p<24;p+=4){const std::uint32_t ref=std::uint32_t(list[p])|(std::uint32_t(list[p+1])<<8)|(std::uint32_t(list[p+2])<<16)|(std::uint32_t(list[p+3])<<24);if(ref==0xffffffffu)continue;auto bytes=read_narc_member(animationNarc,ref);if(bytes.size()>=4&&!std::memcmp(bytes.data(),"BCA0",4)){auto a=parse_nsbca(bytes);if(a.valid)return a;}}
    none.error="no NSBCA animation for build model";return none;
}

NsbmdMatrix sample_nsbca_joint_delta(const NsbmdModel& model,const NsbcaAnimation& animation,int jointIndex,double seconds,double framesPerSecond){
    Mat4f identity=Mat4f::identity();if(!animation.valid||!animation.frameCount||jointIndex<0||size_t(jointIndex)>=model.jointBindWorldMatrices.size())return toPublic(identity);
    const size_t count=model.jointBindWorldMatrices.size();std::vector<Mat4f> local(count),world(count);std::vector<unsigned char> done(count,0);for(size_t i=0;i<count;i++)local[i]=(i<model.jointLocalMatrices.size())?fromPublic(model.jointLocalMatrices[i]):Mat4f::identity();
    const size_t frame=size_t(std::uint64_t(std::max(0.0,std::floor(seconds*framesPerSecond)))%animation.frameCount);
    for(auto const& t:animation.tracks)if(t.jointIndex>=0&&size_t(t.jointIndex)<count&&!t.localFrames.empty())local[size_t(t.jointIndex)]=fromPublic(t.localFrames[std::min(frame,t.localFrames.size()-1)]);
    std::function<Mat4f(size_t)> calc=[&](size_t i)->Mat4f{if(done[i])return world[i];int parent=(i<model.jointParents.size())?model.jointParents[i]:-1;if(parent>=0&&size_t(parent)<count&&size_t(parent)!=i)world[i]=mmul(calc(size_t(parent)),local[i]);else world[i]=local[i];done[i]=1;return world[i];};
    Mat4f animated=calc(size_t(jointIndex)),bind=fromPublic(model.jointBindWorldMatrices[size_t(jointIndex)]);return toPublic(mmul(animated,inverseAffine(bind)));
}

Vec3f transform_nsbmd_point(const NsbmdMatrix& matrix,Vec3f point){return transformPoint(fromPublic(matrix),point);}
Vec3f transform_nsbmd_normal(const NsbmdMatrix& matrix,Vec3f normal){return transformNormal(fromPublic(matrix),normal);}

NsbmdMember load_nsbmd_from_narc(const std::filesystem::path& path,std::size_t index){auto b=read_narc_member(path,index);if(b.empty()){NsbmdMember o;o.error="NARC member missing";return o;}return parse_nsbmd(b);}
NsbmdMember load_nitro_texture_from_narc(const std::filesystem::path& path,std::size_t index){auto b=read_narc_member(path,index);if(b.empty()){NsbmdMember o;o.error="NARC member missing";return o;}return parse_nitro_texture_container(b);}
NsbmdBatchStats validate_nsbmd_narc(const std::filesystem::path& path,std::size_t maxMembers){NsbmdBatchStats s;auto ni=inspect_narc(path);if(!ni.valid)return s;s.members=std::min(maxMembers,ni.members.size());for(size_t i=0;i<s.members;i++){auto m=load_nsbmd_from_narc(path,i);if(!m.valid){s.failures++;continue;}s.parsedMembers++;s.models+=m.models.size();s.textures+=m.textures.size();for(auto const& x:m.models){s.triangles+=x.triangles.size();s.vertices+=x.sourceVertices;}}return s;}
bool export_model_obj(const NsbmdModel& m,const std::filesystem::path& path){std::error_code ec;if(path.has_parent_path())std::filesystem::create_directories(path.parent_path(),ec);std::ofstream f(path);if(!f)return false;f<<"# HeartGold native-port NSBMD geometry export\n# model "<<m.name<<"\n";size_t idx=1;for(auto const& t:m.triangles){for(auto const* v:{&t.a,&t.b,&t.c})f<<"v "<<v->position.x<<' '<<v->position.y<<' '<<v->position.z<<"\n";for(auto const* v:{&t.a,&t.b,&t.c})f<<"vt "<<v->uv.x<<' '<<v->uv.y<<"\n";for(auto const* v:{&t.a,&t.b,&t.c})f<<"vn "<<v->normal.x<<' '<<v->normal.y<<' '<<v->normal.z<<"\n";f<<"f "<<idx<<'/'<<idx<<'/'<<idx<<' '<<idx+1<<'/'<<idx+1<<'/'<<idx+1<<' '<<idx+2<<'/'<<idx+2<<'/'<<idx+2<<"\n";idx+=3;}return bool(f);}


bool export_texture_ppm(const NsbmdTexture& t,const std::filesystem::path& path){if(!t.width||!t.height||t.rgba.size()!=size_t(t.width)*t.height*4)return false;std::error_code ec;if(path.has_parent_path())std::filesystem::create_directories(path.parent_path(),ec);std::ofstream f(path,std::ios::binary);if(!f)return false;f<<"P6\n"<<t.width<<" "<<t.height<<"\n255\n";for(size_t i=0;i<t.rgba.size();i+=4){unsigned a=t.rgba[i+3];unsigned char rgb[3]={static_cast<unsigned char>((unsigned(t.rgba[i])*a)/255),static_cast<unsigned char>((unsigned(t.rgba[i+1])*a)/255),static_cast<unsigned char>((unsigned(t.rgba[i+2])*a)/255)};f.write(reinterpret_cast<const char*>(rgb),3);}return bool(f);}
