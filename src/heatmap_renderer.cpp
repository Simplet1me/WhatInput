#include "heatmap_renderer.h"
#include <QMetaObject>
#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
using Cancel=std::function<bool()>;
// Three separable box passes approximate a Gaussian without per-point kernels.
bool smooth(std::vector<float>& pixels,int width,int height,int radius,const Cancel& cancel){
    std::vector<float> temporary(pixels.size());
    for(int pass=0;pass<3;++pass)for(int axis=0;axis<2;++axis){
        const int length=axis?height:width,lines=axis?width:height;
        auto index=[=](int line,int p){return axis?size_t(p)*width+line:size_t(line)*width+p;};
        for(int line=0;line<lines;++line){
            if(cancel())return false;
            double sum=0;
            for(int p=0;p<=std::min(radius,length-1);++p)sum+=pixels[index(line,p)];
            for(int p=0;p<length;++p){
                const int lo=std::max(0,p-radius),hi=std::min(length-1,p+radius);
                temporary[index(line,p)]=float(sum/(hi-lo+1));
                if(p-radius>=0)sum-=pixels[index(line,p-radius)];
                if(p+radius+1<length)sum+=pixels[index(line,p+radius+1)];
            }
        }
        pixels.swap(temporary);
    }
    return true;
}
struct AxisBlend {int first=0,second=0;double mix=0;};
AxisBlend boundaryBlend(double coordinate,int length,int cellSize,int requestedWidth){
    const int cells=(length+cellSize-1)/cellSize;
    const int at=std::clamp(int(coordinate)/cellSize,0,cells-1);
    const double blend=std::min(double(requestedWidth),cellSize/2.0-0.25);
    if(blend<=0)return {at,at,0};
    const double left=at*cellSize,right=std::min((at+1)*cellSize,length);
    int first=at,second=at;double t=0;
    if(at>0&&coordinate-left<blend){first=at-1;second=at;t=(coordinate-left+blend)/(2*blend);}
    else if(at+1<cells&&right-coordinate<blend){first=at;second=at+1;t=(coordinate-right+blend)/(2*blend);}
    else return {at,at,0};
    t=std::clamp(t,0.0,1.0);
    return {first,second,t*t*(3-2*t)};
}
std::shared_ptr<const HeatmapDensity> density(const wi::ViewResult::MouseScreen& source,const HeatmapSettings& settings,const std::shared_ptr<const HeatmapBatch>& previous,const Cancel& cancel){
    std::vector<wi::ViewResult::MousePoint> points;
    for(const auto& point:source.points)if(point.button==settings.button||(settings.button==-1&&(point.button==0||point.button==1)))points.push_back(point);
    if(previous)for(const auto& frame:previous->frames){
        const auto& field=frame.density;
        if(field->screen==source.screen&&field->button==settings.button&&field->cellSize==settings.cellSize&&field->edgeBlend==settings.edgeBlend&&field->grid==settings.grid&&field->radius==settings.radius&&field->points==points)return field;
    }
    auto field=std::make_shared<HeatmapDensity>();field->screen=source.screen;field->button=settings.button;field->cellSize=settings.cellSize;field->edgeBlend=settings.edgeBlend;field->points=std::move(points);
    const int physicalWidth=std::max(1,source.screen.width),physicalHeight=std::max(1,source.screen.height);
    const double scale=std::min({1.0,4096.0/physicalWidth,2160.0/physicalHeight});
    field->width=std::max(1,int(std::round(physicalWidth*scale)));field->height=std::max(1,int(std::round(physicalHeight*scale)));
    field->grid=settings.grid;field->radius=settings.radius;
    if(!settings.grid){
        field->values.resize(size_t(field->width)*field->height);
        for(const auto& point:field->points){
            if(cancel())return {};
            const double px=std::clamp((point.x+0.5)*field->width/physicalWidth-0.5,0.0,double(field->width-1));
            const double py=std::clamp((point.y+0.5)*field->height/physicalHeight-0.5,0.0,double(field->height-1));
            const int x=int(px),y=int(py);const double tx=px-x,ty=py-y;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                field->values[size_t(std::min(y+dy,field->height-1))*field->width+std::min(x+dx,field->width-1)]+=float(double(point.count)*(dx?tx:1-tx)*(dy?ty:1-ty));
        }
        if(!smooth(field->values,field->width,field->height,std::max(1,int(std::round(settings.radius*scale/3))),cancel))return {};
        const double area=double(physicalWidth)*physicalHeight/(double(field->width)*field->height);
        for(auto& value:field->values){value=float(value/area);field->maximum=std::max(field->maximum,double(value));}
        return field;
    }
    field->columns=(physicalWidth+settings.cellSize-1)/settings.cellSize;
    field->rows=(physicalHeight+settings.cellSize-1)/settings.cellSize;
    field->cellCounts.resize(size_t(field->columns)*field->rows);
    // Exact integer count per physical grid cell. No centroid relocation,
    // Gaussian spreading, mirrored points, or cross-screen mixing.
    for(const auto& point:field->points){
        if(cancel())return {};
        const int x=std::clamp(point.x/settings.cellSize,0,field->columns-1),y=std::clamp(point.y/settings.cellSize,0,field->rows-1);
        field->cellCounts[size_t(y)*field->columns+x]+=point.count;
    }
    const double nominalArea=double(settings.cellSize)*settings.cellSize;
    field->values.resize(field->cellCounts.size());
    for(size_t i=0;i<field->values.size();++i){
        field->values[i]=float(double(field->cellCounts[i])/nominalArea);
        field->maximum=std::max(field->maximum,double(field->values[i]));
    }
    return field;
}
std::vector<float> rasterDensity(const HeatmapDensity& field,const Cancel& cancel){
    if(!field.grid)return field.values;
    std::vector<AxisBlend> xs(field.width),ys(field.height);
    for(int x=0;x<field.width;++x)xs[x]=boundaryBlend((x+0.5)*field.screen.width/field.width,field.screen.width,field.cellSize,field.edgeBlend);
    for(int y=0;y<field.height;++y)ys[y]=boundaryBlend((y+0.5)*field.screen.height/field.height,field.screen.height,field.cellSize,field.edgeBlend);
    std::vector<float> pixels(size_t(field.width)*field.height);
    for(int y=0;y<field.height;++y){
        if(cancel())return {};
        const auto& by=ys[y];
        for(int x=0;x<field.width;++x){
            const auto& bx=xs[x];
            const double top=field.values[size_t(by.first)*field.columns+bx.first]*(1-bx.mix)+field.values[size_t(by.first)*field.columns+bx.second]*bx.mix;
            const double bottom=field.values[size_t(by.second)*field.columns+bx.first]*(1-bx.mix)+field.values[size_t(by.second)*field.columns+bx.second]*bx.mix;
            pixels[size_t(y)*field.width+x]=float(top*(1-by.mix)+bottom*by.mix);
        }
    }
    return pixels;
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
        auto native=rasterDensity(field,cancel);if(cancel())return {};
        const std::vector<float>* values=&native;
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
    settings.button=settings.button==0||settings.button==1?settings.button:-1;settings.cellSize=std::clamp(settings.cellSize,5,100);settings.edgeBlend=std::clamp(settings.edgeBlend,0,4);settings.strength=std::clamp(settings.strength,25,400);
    settings.radius=std::clamp(settings.radius,1,200);
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
