#include <3ds.h>
#include <citro2d.h>
#include <tex3ds.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

namespace {
constexpr int TOP_W=400, TOP_H=240;
constexpr int COLS=4, ROWS=2, PAGE_SIZE=COLS*ROWS;
constexpr const char* PHOTO_DIR="sdmc:/3ds/MaGalerie/photos";

constexpr u32 BG=C2D_Color32(8,12,20,255);
constexpr u32 PANEL=C2D_Color32(22,29,42,255);
constexpr u32 CARD=C2D_Color32(31,39,54,255);
constexpr u32 TEXT=C2D_Color32(246,248,252,255);
constexpr u32 MUTED=C2D_Color32(154,166,186,255);
constexpr u32 ACCENT=C2D_Color32(62,181,255,255);
constexpr u32 SHADOW=C2D_Color32(0,0,0,105);
constexpr u32 ERROR=C2D_Color32(255,108,118,255);

enum class Mode { Grid, Viewer };

struct Bitmap {
    int w=0,h=0;
    std::vector<u8> rgba;
    bool valid() const { return w>0 && h>0 && !rgba.empty(); }
};

struct Texture {
    C3D_Tex tex{};
    Tex3DS_SubTexture sub{};
    C2D_Image img{};
    int w=0,h=0;
    bool ready=false;

    Texture()=default;
    Texture(const Texture&)=delete;
    Texture& operator=(const Texture&)=delete;
    Texture(Texture&& o) noexcept { *this=std::move(o); }
    Texture& operator=(Texture&& o) noexcept {
        if(this==&o) return *this;
        clear();
        tex=o.tex; sub=o.sub; img=o.img; w=o.w; h=o.h; ready=o.ready;
        if(ready) img.tex=&tex;
        o.ready=false; o.img.tex=nullptr;
        return *this;
    }
    ~Texture(){ clear(); }
    void clear(){ if(ready){ C3D_TexDelete(&tex); ready=false; } img.tex=nullptr; }
};

u16 r16(FILE* f){u8 b[2]{}; return fread(b,1,2,f)==2?u16(b[0]|(b[1]<<8)):0;}
u32 r32(FILE* f){u8 b[4]{}; return fread(b,1,4,f)==4?u32(b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24)):0;}
s32 rs32(FILE* f){return static_cast<s32>(r32(f));}

bool isBmp(std::string s){
    if(s.size()<4) return false;
    s=s.substr(s.size()-4);
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});
    return s==".bmp";
}
std::string baseName(const std::string& p){
    auto i=p.find_last_of("/\\"); return i==std::string::npos?p:p.substr(i+1);
}
std::vector<std::string> listPhotos(){
    std::vector<std::string> v;
    DIR* d=opendir(PHOTO_DIR); if(!d) return v;
    while(dirent* e=readdir(d)){
        std::string n=e->d_name;
        if(n!="."&&n!=".."&&isBmp(n)) v.push_back(std::string(PHOTO_DIR)+"/"+n);
    }
    closedir(d); std::sort(v.begin(),v.end()); return v;
}

bool loadBmp(const std::string& path, Bitmap& out, std::string& err){
    FILE* f=fopen(path.c_str(),"rb");
    if(!f){err="Impossible d'ouvrir la photo"; return false;}
    if(r16(f)!=0x4D42){fclose(f);err="Fichier BMP invalide";return false;}
    r32(f);r16(f);r16(f);u32 offset=r32(f);u32 dib=r32(f);
    if(dib<40){fclose(f);err="BMP non compatible";return false;}
    s32 w=rs32(f), sh=rs32(f);u16 planes=r16(f),bpp=r16(f);u32 comp=r32(f);
    if(w<=0||sh==0||planes!=1||(bpp!=24&&bpp!=32)||comp!=0){
        fclose(f);err="BMP 24/32 bits non compresse requis";return false;
    }
    int h=sh<0?-sh:sh; bool top=sh<0;
    if(w>4096||h>4096){fclose(f);err="Photo trop grande";return false;}
    int bytes=bpp/8; size_t rowSize=((size_t(w)*bpp+31)/32)*4;
    if(fseek(f,long(offset),SEEK_SET)!=0){fclose(f);err="BMP endommage";return false;}
    out.w=w;out.h=h;out.rgba.assign(size_t(w)*h*4,255);
    std::vector<u8> row(rowSize);
    for(int sy=0;sy<h;sy++){
        if(fread(row.data(),1,rowSize,f)!=rowSize){fclose(f);err="Lecture incomplete";return false;}
        int y=top?sy:h-1-sy;
        for(int x=0;x<w;x++){
            size_t s=size_t(x)*bytes,d=(size_t(y)*w+x)*4;
            out.rgba[d]=row[s+2];out.rgba[d+1]=row[s+1];out.rgba[d+2]=row[s];
            out.rgba[d+3]=bpp==32?row[s+3]:255;
        }
    }
    fclose(f);return true;
}

Bitmap resizeFit(const Bitmap& src,int mw,int mh){
    Bitmap o;if(!src.valid())return o;
    float scale=std::min(float(mw)/src.w,float(mh)/src.h);
    scale=std::min(scale,1.0f);
    o.w=std::max(1,int(src.w*scale));o.h=std::max(1,int(src.h*scale));
    o.rgba.resize(size_t(o.w)*o.h*4);
    for(int y=0;y<o.h;y++){
        int sy=std::clamp(int(y/scale),0,src.h-1);
        for(int x=0;x<o.w;x++){
            int sx=std::clamp(int(x/scale),0,src.w-1);
            memcpy(&o.rgba[(size_t(y)*o.w+x)*4],&src.rgba[(size_t(sy)*src.w+sx)*4],4);
        }
    }
    return o;
}
int pow2(int x){int p=8;while(p<x)p<<=1;return p;}

bool upload(const Bitmap& b,Texture& o){
    o.clear();if(!b.valid())return false;
    int tw=pow2(b.w),th=pow2(b.h);if(tw>1024||th>1024)return false;
    std::vector<u8> padded(size_t(tw)*th*4,0);
    for(int y=0;y<b.h;y++) memcpy(&padded[size_t(y)*tw*4],&b.rgba[size_t(y)*b.w*4],size_t(b.w)*4);
    if(!C3D_TexInit(&o.tex,tw,th,GPU_RGBA8))return false;
    C3D_TexSetFilter(&o.tex,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&o.tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    C3D_TexUpload(&o.tex,padded.data());
    o.sub={u16(b.w),u16(b.h),0.0f,1.0f,float(b.w)/tw,1.0f-float(b.h)/th};
    o.img={&o.tex,&o.sub};o.w=b.w;o.h=b.h;o.ready=true;return true;
}
bool loadTexture(const std::string& p,int mw,int mh,Texture& t,std::string& e){
    Bitmap b;if(!loadBmp(p,b,e))return false;return upload(resizeFit(b,mw,mh),t);
}

void panel(float x,float y,float w,float h,float r,u32 c){
    C2D_DrawRectSolid(x+r,y,0.1f,w-2*r,h,c);C2D_DrawRectSolid(x,y+r,0.1f,w,h-2*r,c);
    C2D_DrawCircleSolid(x+r,y+r,0.1f,r,c);C2D_DrawCircleSolid(x+w-r,y+r,0.1f,r,c);
    C2D_DrawCircleSolid(x+r,y+h-r,0.1f,r,c);C2D_DrawCircleSolid(x+w-r,y+h-r,0.1f,r,c);
}
void text(C2D_TextBuf buf,const std::string& s,float x,float y,float scale,u32 color){
    C2D_Text t;C2D_TextParse(&t,buf,s.c_str());C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,x,y,0.7f,scale,scale,color);
}
void centered(C2D_TextBuf buf,const std::string& s,float cx,float y,float scale,u32 color){
    C2D_Text t;C2D_TextParse(&t,buf,s.c_str());C2D_TextOptimize(&t);float w=0,h=0;
    C2D_TextGetDimensions(&t,scale,scale,&w,&h);
    C2D_DrawText(&t,C2D_WithColor,cx-w/2,y,0.7f,scale,scale,color);
}
void hint(C2D_TextBuf buf,float x,float y,const char* key,const char* label){
    C2D_DrawCircleSolid(x+8,y+8,0.4f,8,ACCENT);centered(buf,key,x+8,y+1,0.42f,C2D_Color32(3,18,30,255));
    text(buf,label,x+21,y+1,0.43f,TEXT);
}

struct App{
    C3D_RenderTarget* top=nullptr,*bottom=nullptr;
    C2D_TextBuf font=nullptr;
    std::vector<std::string> files;
    std::vector<Texture> thumbs;
    Texture viewer;
    size_t selected=0,page=0;
    Mode mode=Mode::Grid;
    std::string error;
    bool pageDirty=true,viewerDirty=false;
    float pulse=0,fade=1,zoom=1;

    bool init(){
        gfxInitDefault();C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);C2D_Init(C2D_DEFAULT_MAX_OBJECTS);C2D_Prepare();
        top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);bottom=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
        font=C2D_TextBufNew(4096);files=listPhotos();return top&&bottom&&font;
    }
    void stop(){thumbs.clear();viewer.clear();C2D_TextBufDelete(font);C2D_Fini();C3D_Fini();gfxExit();}
    void refresh(){files=listPhotos();selected=page=0;mode=Mode::Grid;error.clear();pageDirty=true;viewerDirty=false;}
    void loadPage(){
        thumbs.clear();error.clear();size_t first=page*PAGE_SIZE,last=std::min(first+PAGE_SIZE,files.size());
        thumbs.reserve(last-first);
        for(size_t i=first;i<last;i++){Texture t;std::string e;if(!loadTexture(files[i],160,124,t,e)&&error.empty())error=e;thumbs.push_back(std::move(t));}
        pageDirty=false;
    }
    void loadView(){viewer.clear();error.clear();if(!files.empty())loadTexture(files[selected],800,480,viewer,error);viewerDirty=false;}
    void update(u32 down,float dt){
        pulse+=dt*5.0f;fade=std::min(1.0f,fade+dt*6.0f);
        if(down&KEY_X)refresh();
        if(mode==Mode::Grid&&!files.empty()){
            size_t old=selected;
            if((down&KEY_LEFT)&&selected%COLS>0)--selected;
            if((down&KEY_RIGHT)&&selected+1<files.size()&&selected%COLS<COLS-1)++selected;
            if((down&KEY_UP)&&selected>=COLS)selected-=COLS;
            if((down&KEY_DOWN)&&selected+COLS<files.size())selected+=COLS;
            if(down&KEY_L)selected=selected>=PAGE_SIZE?selected-PAGE_SIZE:0;
            if(down&KEY_R)selected=std::min(selected+PAGE_SIZE,files.size()-1);
            if(selected!=old){size_t p=selected/PAGE_SIZE;if(p!=page){page=p;pageDirty=true;}}
            if(down&KEY_A){mode=Mode::Viewer;zoom=1;fade=0;viewerDirty=true;}
        }else if(mode==Mode::Viewer){
            if(down&KEY_B){mode=Mode::Grid;fade=0;}
            if(!files.empty()){
                if(down&KEY_RIGHT){selected=(selected+1)%files.size();zoom=1;viewerDirty=true;}
                if(down&KEY_LEFT){selected=(selected+files.size()-1)%files.size();zoom=1;viewerDirty=true;}
                if(down&KEY_UP)zoom=std::min(3.0f,zoom+0.2f);
                if(down&KEY_DOWN)zoom=std::max(0.6f,zoom-0.2f);
            }
        }
        if(mode==Mode::Grid&&pageDirty)loadPage();
        if(mode==Mode::Viewer&&viewerDirty)loadView();
    }
    void drawGrid(){
        C2D_TargetClear(top,BG);C2D_SceneBegin(top);
        C2D_DrawRectSolid(0,0,0.1f,TOP_W,34,PANEL);
        text(font,"MaGalerie",13,7,0.65f,TEXT);text(font,std::to_string(files.size())+" photos",310,10,0.40f,MUTED);
        if(files.empty()){panel(46,74,308,108,16,PANEL);centered(font,"Aucune photo trouvee",200,96,0.66f,TEXT);centered(font,PHOTO_DIR,200,132,0.40f,MUTED);return;}
        size_t first=page*PAGE_SIZE;float anim=1.0f+std::sin(pulse)*0.014f;
        for(int local=0;local<PAGE_SIZE;local++){
            size_t global=first+local;if(global>=files.size())break;
            int col=local%COLS,row=local/COLS;float x=10+col*97,y=42+row*94;bool active=global==selected;
            float s=active?anim:1.0f,w=88*s,h=84*s,cx=x-(w-88)/2,cy=y-(h-84)/2;
            if(active)panel(cx-2,cy-2,w+4,h+4,10,ACCENT);panel(cx+2,cy+3,w,h,8,SHADOW);panel(cx,cy,w,h,8,CARD);
            size_t ti=global-first;if(ti<thumbs.size()&&thumbs[ti].ready){
                auto& im=thumbs[ti];float fit=std::min(80.0f/im.w,59.0f/im.h)*s,dw=im.w*fit,dh=im.h*fit;
                C2D_DrawImageAt(im.img,cx+(w-dw)/2,cy+5+(59*s-dh)/2,0.5f,nullptr,fit,fit);
            }
            text(font,baseName(files[global]).substr(0,13),cx+6,cy+h-17,0.32f,active?TEXT:MUTED);
        }
        float pages=std::max(1.0f,std::ceil(files.size()/float(PAGE_SIZE))),pw=360/pages;
        panel(20,230,360,4,2,C2D_Color32(31,39,53,255));panel(20+pw*page,230,pw,4,2,ACCENT);
    }
    void drawViewer(){
        C2D_TargetClear(top,C2D_Color32(2,3,7,255));C2D_SceneBegin(top);
        if(viewer.ready){
            float base=std::min(float(TOP_W)/viewer.w,float(TOP_H)/viewer.h),scale=base*zoom*(0.94f+0.06f*fade);
            float w=viewer.w*scale,h=viewer.h*scale;C2D_DrawImageAt(viewer.img,(TOP_W-w)/2,(TOP_H-h)/2,0.3f,nullptr,scale,scale);
        }
        C2D_DrawRectSolid(0,0,0.6f,TOP_W,28,C2D_Color32(0,0,0,155));
        text(font,baseName(files[selected]).substr(0,30),10,6,0.44f,TEXT);
        text(font,std::to_string(selected+1)+" / "+std::to_string(files.size()),344,7,0.36f,MUTED);
    }
    void drawBottom(){
        C2D_TargetClear(bottom,BG);C2D_SceneBegin(bottom);
        panel(10,10,300,76,12,PANEL);text(font,mode==Mode::Grid?"Galerie":"Apercu plein ecran",24,23,0.64f,TEXT);
        text(font,files.empty()?"Ajoute des photos BMP":baseName(files[selected]).substr(0,31),24,54,0.41f,MUTED);
        if(!error.empty()){panel(10,96,300,38,10,C2D_Color32(66,25,32,255));text(font,error.substr(0,39),20,107,0.38f,ERROR);}
        panel(10,146,300,82,12,PANEL);
        if(mode==Mode::Grid){hint(font,22,160,"A","Ouvrir");hint(font,150,160,"X","Actualiser");hint(font,22,194,"L","Page -");hint(font,150,194,"R","Page +");}
        else{hint(font,22,160,"B","Retour");hint(font,150,160,"^","Zoom +");hint(font,22,194,"<","Precedente");hint(font,166,194,">","Suivante");}
    }
    void render(){
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);if(mode==Mode::Grid)drawGrid();else drawViewer();drawBottom();C3D_FrameEnd(0);C2D_TextBufClear(font);
    }
};
}
int main(){
    App app;if(!app.init())return 1;u64 prev=osGetTime();
    while(aptMainLoop()){hidScanInput();u32 down=hidKeysDown();if(down&KEY_START)break;u64 now=osGetTime();float dt=std::min(0.05f,float(now-prev)/1000.0f);prev=now;app.update(down,dt);app.render();}
    app.stop();return 0;
}
