#pragma once
#include "engine.h"
#include <QImage>
#include <QObject>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

struct HeatmapSettings {
    int button=-1, cellSize=10, strength=100;
    bool logarithmic=true;
    int edgeBlend=0;
    bool grid=true;
    int radius=24;
    bool operator==(const HeatmapSettings&)const=default;
};
using HeatmapSources=std::vector<wi::ViewResult::MouseScreen>;
struct HeatmapDensity {
    wi::MousePosition screen;
    int width=0,height=0,button=-1,cellSize=10,edgeBlend=0,columns=0,rows=0,radius=24;
    bool grid=true;
    std::vector<wi::ViewResult::MousePoint> points;
    std::vector<float> values;
    std::vector<wi::Count> cellCounts;
    double maximum=0;
};
struct HeatmapFrame {
    std::shared_ptr<const HeatmapDensity> density;
    std::vector<QImage> levels;
};
struct HeatmapBatch {
    std::shared_ptr<const HeatmapSources> sources;
    HeatmapSettings settings;
    double sharedMaximum=0;
    std::vector<HeatmapFrame> frames;
};

// One bounded worker per visible statistics/overlay owner; newer jobs replace
// pending jobs. Only completed QImages cross back to the GUI thread.
class HeatmapRenderer : public QObject {
public:
    explicit HeatmapRenderer(QObject* parent=nullptr);
    ~HeatmapRenderer()override;
    void request(std::shared_ptr<const HeatmapSources> sources,HeatmapSettings settings,quint64 scope,std::shared_ptr<const HeatmapBatch> seed={});
    void clear();
    std::function<void(std::shared_ptr<const HeatmapBatch>)> ready;
private:
    struct Job {
        std::shared_ptr<const HeatmapSources> sources;
        HeatmapSettings settings;
        quint64 scope=0,serial=0,generation=0;
        std::shared_ptr<const HeatmapBatch> seed;
    };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Job> pending_;
    bool resetCache_=false;
    std::atomic<bool> stopping_=false;
    std::atomic<quint64> generation_=0;
    quint64 serial_=0,scope_=0,shown_=0;
    HeatmapSettings settings_;
    std::thread worker_;
    void run();
};
