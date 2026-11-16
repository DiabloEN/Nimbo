#include "background.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QFile>
#include <QScopeGuard>
#include <cmath>
#include <stdexcept>
// Native MediaPipe Tasks C ABI from the pinned 0.10.32 Windows distribution.
// Keep the engine and original selfie model unloaded while the effect is off.
namespace {
struct Base{const char* bytes;unsigned length;const char* path;int delegate;};
struct Options{Base base;int mode;const char* locale;bool confidence,category;void* callback;};
struct Result{void** masks;unsigned count;void* category;unsigned hasCategory;float* scores;unsigned numScores;};
using Handle=void*;
}
struct BackgroundBlur::Engine {
    QLibrary lib{QCoreApplication::applicationDirPath()+"/libmediapipe.dll"};Handle segmenter=nullptr;QByteArray model;
    template<typename T>T symbol(const char* name){auto p=lib.resolve(name);if(!p)throw std::runtime_error("MediaPipe runtime unavailable");return reinterpret_cast<T>(p);}
    void (*freeError)(char*)=symbol<decltype(freeError)>("MpErrorFree");
    int (*create)(Options*,Handle*,char**)=symbol<decltype(create)>("MpImageSegmenterCreate");
    int (*image)(int,int,int,const uchar*,int,Handle*,char**)=symbol<decltype(image)>("MpImageCreateFromUint8Data");
    int (*segment)(Handle,Handle,void*,Result*,char**)=symbol<decltype(segment)>("MpImageSegmenterSegmentImage");
    int (*data)(Handle,const float**,char**)=symbol<decltype(data)>("MpImageDataFloat32");
    int (*width)(Handle)=symbol<decltype(width)>("MpImageGetWidth");
    int (*height)(Handle)=symbol<decltype(height)>("MpImageGetHeight");
    int (*stride)(Handle)=symbol<decltype(stride)>("MpImageGetWidthStep");
    void (*release)(Handle)=symbol<decltype(release)>("MpImageFree");
    void (*releaseResult)(Result*)=symbol<decltype(releaseResult)>("MpImageSegmenterCloseResult");
    int (*close)(Handle,char**)=symbol<decltype(close)>("MpImageSegmenterClose");
    void check(int status,char*& error){if(error){freeError(error);error=nullptr;}if(status)throw std::runtime_error("MediaPipe inference failed");}
    Engine(){QFile f(QCoreApplication::applicationDirPath()+"/assets/selfie_segmenter.tflite");if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Missing model");model=f.readAll();Options o{{model.constData(),unsigned(model.size()),nullptr,0},1,"en",true,false,nullptr};char* err=nullptr;check(create(&o,&segmenter,&err),err);}
    ~Engine(){
        if(segmenter){char* error=nullptr;close(segmenter,&error);if(error)freeError(error);}
        lib.unload();
    }
    QImage mask(const QImage& original){
        auto small=original.scaledToWidth(256,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
        Handle input=nullptr;char* err=nullptr;check(image(2,small.width(),small.height(),small.constBits(),int(small.sizeInBytes()),&input,&err),err);
        Result r{};int status=segment(segmenter,input,nullptr,&r,&err);release(input);check(status,err);
        auto cleanup=qScopeGuard([&]{releaseResult(&r);});if(!r.count)throw std::runtime_error("No mask");Handle m=r.masks[r.count-1];const float* ptr=nullptr;check(data(m,&ptr,&err),err);
        const int w=width(m),h=height(m),step=stride(m)/int(sizeof(float));QImage mask(w,h,QImage::Format_Grayscale8);
        for(int y=0;y<h;++y){auto dst=mask.scanLine(y);for(int x=0;x<w;++x)dst[x]=uchar(qBound(0,int(ptr[y*step+x]*255),255));}return mask;
    }
};
BackgroundBlur::BackgroundBlur()=default;BackgroundBlur::~BackgroundBlur()=default;
QImage BackgroundBlur::apply(const QImage& image,int radius){
    if(image.isNull())return {};if(!engine)engine=std::make_unique<Engine>();
    auto rgba=image.convertToFormat(QImage::Format_RGBA8888);auto mask=engine->mask(rgba).scaled(rgba.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    int scale=qBound(2,radius,60);auto blur=rgba.scaled(qMax(2,rgba.width()/scale),qMax(2,rgba.height()/scale),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).scaled(rgba.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    for(int y=0;y<rgba.height();++y){auto dst=rgba.scanLine(y);const auto bg=blur.constScanLine(y),alpha=mask.constScanLine(y);for(int x=0;x<rgba.width();++x){int a=alpha[x];for(int k=0;k<3;++k){int i=4*x+k;dst[i]=uchar((dst[i]*a+bg[i]*(255-a)+127)/255);}}}
    return rgba;
}
