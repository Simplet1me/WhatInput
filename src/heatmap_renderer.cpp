#include "heatmap_renderer.h"
#include <QMetaObject>
#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
using Cancel=std::function<bool()>;
int reflected(int index,int size){
    if(size<=1)return 0;
    const int period=2*size;index%=period;if(index<0)index+=period;
    return index<size?index:period-1-index;
}
std::array<int,3> boxRadii(double sigma){
    // Three normalized box convolutions approximate a Gaussian, in O(pixels)
    // time regardless of bandwidth. Reflection keeps edge clicks in the image.
    const double ideal=std::sqrt(4*sigma*sigma+1);
    int lower=std::max(1,int(std::floor(ideal)));if(lower%2==0)--lower;
    const double middle=(12*sigma*sigma-3*lower*lower-12*lower-9)/(-4*lower-4);
    const int small=std::clamp(int(std::round(middle)),0,3);
    std::array<int,3> result{};for(int i=0;i<3;++i)result[i]=((i<small?lower:lower+2)-1)/2;
    return result;
}
bool blur(const std::vector<float>& input,std::vector<float>& output,int width,int height,int radius,bool horizontal,const Cancel& cancel){
    if(radius==0){output=input;return !cancel();}
    const int length=horizontal?width:height,lines=horizontal?height:width;
    std::vector<double> prefix(size_t(length+2*radius)+1);
    const double inverse=1.0/(2*radius+1);
    for(int line=0;line<lines;++line){
        if(cancel())return false;
        prefix[0]=0;
        for(int i=0;i<length+2*radius;++i){const int coordinate=reflected(i-radius,length);const auto offset=horizontal?size_t(line)*width+coordinate:size_t(coordinate)*width+line;prefix[i+1]=prefix[i]+input[offset];}
        for(int i=0;i<length;++i){const auto offset=horizontal?size_t(line)*width+i:size_t(i)*width+line;output[offset]=float((prefix[i+2*radius+1]-prefix[i])*inverse);}
    }
    return true;
}
std::shared_ptr<const HeatmapDensity> density(const wi::ViewResult::MouseScreen& source,const HeatmapSettings& settings,const std::shared_ptr<const HeatmapBatch>& previous,const Cancel& cancel){
    std::vector<wi::ViewResult::MousePoint> points;
    for(const auto& point:source.points)if(point.button==settings.button||(settings.button==-1&&(point.button==0||point.button==1)))points.push_back(point);
    if(previous)for(const auto& frame:previous->frames){
        const auto& field=frame.density;
        if(field->screen==source.screen&&field->button==settings.button&&field->smoothing==settings.smoothing&&field->points==points)return field;
    }
    auto field=std::make_shared<HeatmapDensity>();field->screen=source.screen;field->button=settings.button;field->smoothing=settings.smoothing;field->points=std::move(points);
    const int physicalWidth=std::max(1,source.screen.width),physicalHeight=std::max(1,source.screen.height);
    const double scale=std::min({1.0,2560.0/physicalWidth,1600.0/physicalHeight});
    field->width=std::max(1,int(std::round(physicalWidth*scale)));field->height=std::max(1,int(std::round(physicalHeight*scale)));
    const int width=field->width,height=field->height;
    field->values.resize(size_t(width)*height);
    // Bilinear deposition retains weighted counts and sub-grid positions.
    for(const auto& point:field->points){
        if(cancel())return {};
        const double x=std::clamp((point.x+0.5)*width/physicalWidth-0.5,0.0,double(width-1)),y=std::clamp((point.y+0.5)*height/physicalHeight-0.5,0.0,double(height-1));
        const int x0=int(x),y0=int(y),x1=std::min(x0+1,width-1),y1=std::min(y0+1,height-1);const double dx=x-x0,dy=y-y0,count=double(point.count);
        field->values[size_t(y0)*width+x0]+=float(count*(1-dx)*(1-dy));field->values[size_t(y0)*width+x1]+=float(count*dx*(1-dy));
        field->values[size_t(y1)*width+x0]+=float(count*(1-dx)*dy);field->values[size_t(y1)*width+x1]+=float(count*dx*dy);
    }
    // The UI value is an influence radius, approximately 3 sigma, rather
    // than sigma itself. Otherwise "28 px" visibly spreads over ~84 px.
    const double sigma=double(settings.smoothing)/3.0;
    auto horizontal=boxRadii(std::max(0.5,sigma*width/physicalWidth));
    auto vertical=boxRadii(std::max(0.5,sigma*height/physicalHeight));
    std::vector<float> temporary(field->values.size());
    for(int pass=0;pass<3;++pass){
        if(!blur(field->values,temporary,width,height,horizontal[pass],true,cancel)||!blur(temporary,field->values,width,height,vertical[pass],false,cancel))return {};
    }
    // Values are clicks per physical pixel, so different screen resolutions
    // and raster sizes can share a meaningful color scale.
    const double pixelArea=double(physicalWidth)*physicalHeight/(double(width)*height);
    for(auto& value:field->values){value=float(std::max(0.0,double(value))/pixelArea);field->maximum=std::max(field->maximum,double(value));}
    return field;
}
std::array<QRgb,1024> palette(){
    const std::array<QColor,6> colors={QColor("#101187"),QColor("#0758ab"),QColor("#009f91"),QColor("#49bd48"),QColor("#e4e42c"),QColor("#f02b1c")};
    std::array<QRgb,1024> result{};
    for(int i=0;i<1024;++i){const double position=double(i)/1023*5;const int at=std::min(4,int(position));const double t=position-at;
        result[i]=qRgb(int(colors[at].red()*(1-t)+colors[at+1].red()*t),int(colors[at].green()*(1-t)+colors[at+1].green()*t),int(colors[at].blue()*(1-t)+colors[at+1].blue()*t));}
    return result;
}
std::shared_ptr<const HeatmapBatch> render(const std::shared_ptr<const HeatmapSources>& sources,const HeatmapSettings& settings,const std::shared_ptr<const HeatmapBatch>& previous,const Cancel& cancel){
    auto batch=std::make_shared<HeatmapBatch>();batch->sources=sources;batch->settings=settings;
    for(const auto& source:*sources){auto field=density(source,settings,previous,cancel);if(!field)return {};batch->sharedMaximum=std::max(batch->sharedMaximum,field->maximum);batch->frames.push_back({std::move(field),{}});}
    static const auto colors=palette();
    for(auto& frame:batch->frames){
        if(cancel())return {};
        bool reused=false;
        if(previous&&previous->sharedMaximum==batch->sharedMaximum&&previous->settings.strength==settings.strength&&previous->settings.logarithmic==settings.logarithmic)
            for(const auto& old:previous->frames)if(old.density==frame.density){frame.levels=old.levels;reused=true;break;}
        if(reused)continue;
        const auto& field=*frame.density;
        const std::vector<float>* values=&field.values;
        std::vector<float> reduced;
        int width=field.width,height=field.height;
        for(;;){
            QImage image(width,height,QImage::Format_RGB32);
            for(int y=0;y<height;++y){
                if(cancel())return {};
                auto row=reinterpret_cast<QRgb*>(image.scanLine(y));
                for(int x=0;x<width;++x){
                    double value=batch->sharedMaximum?std::clamp(double((*values)[size_t(y)*width+x])/batch->sharedMaximum*(settings.strength/100.0),0.0,1.0):0;
                    if(settings.logarithmic)value=std::log1p(99*value)/std::log(100.0);
                    row[x]=colors[std::clamp(int(value*1023),0,1023)];
                }
            }
            frame.levels.push_back(std::move(image));
            if(width<=320&&height<=200)break;
            // Reduce density before applying the color ramp. Averaging RGB
            // heat colors repeatedly adds haze and can create false hues.
            const int nextWidth=(width+1)/2,nextHeight=(height+1)/2;
            std::vector<float> next(size_t(nextWidth)*nextHeight);
            for(int y=0;y<nextHeight;++y){
                if(cancel())return {};
                for(int x=0;x<nextWidth;++x){
                    double sum=0;int samples=0;
                    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
                        const int sx=2*x+dx,sy=2*y+dy;
                        if(sx<width&&sy<height){sum+=(*values)[size_t(sy)*width+sx];++samples;}
                    }
                    next[size_t(y)*nextWidth+x]=float(sum/samples);
                }
            }
            reduced=std::move(next);values=&reduced;width=nextWidth;height=nextHeight;
        }
    }
    return batch;
}
}

HeatmapRenderer::HeatmapRenderer(QObject* parent):QObject(parent){worker_=std::thread([this]{run();});}
HeatmapRenderer::~HeatmapRenderer(){stopping_=true;wake_.notify_one();if(worker_.joinable())worker_.join();}
void HeatmapRenderer::request(std::shared_ptr<const HeatmapSources> sources,HeatmapSettings settings,quint64 scope,std::shared_ptr<const HeatmapBatch> seed){
    settings.button=settings.button==0||settings.button==1?settings.button:-1;settings.smoothing=std::clamp(settings.smoothing,1,200);settings.strength=std::clamp(settings.strength,25,400);
    settings_=settings;scope_=scope;
    {std::lock_guard lock(mutex_);pending_=Job{std::move(sources),settings,scope,++serial_,generation_.load(),std::move(seed)};}wake_.notify_one();
}
void HeatmapRenderer::clear(){++generation_;{std::lock_guard lock(mutex_);pending_.reset();resetCache_=true;}wake_.notify_one();}
void HeatmapRenderer::run(){
    std::shared_ptr<const HeatmapBatch> previous;
    for(;;){
        Job job;
        {std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_||pending_.has_value()||resetCache_;});if(stopping_)return;if(resetCache_){previous.reset();resetCache_=false;}if(!pending_)continue;job=std::move(*pending_);pending_.reset();}
        const auto cancel=[this,generation=job.generation]{return stopping_||generation_.load()!=generation;};
        try{
            auto batch=render(job.sources,job.settings,job.seed?job.seed:previous,cancel);if(!batch||cancel())continue;previous=batch;
            QMetaObject::invokeMethod(this,[this,batch,job]{if(!stopping_&&generation_.load()==job.generation&&scope_==job.scope&&settings_==job.settings&&job.serial>shown_){shown_=job.serial;if(ready)ready(batch);}},Qt::QueuedConnection);
        }catch(const std::exception& error){previous.reset();qWarning("Heatmap rendering failed: %s",error.what());}
    }
}
