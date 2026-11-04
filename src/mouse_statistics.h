#pragma once
#include "engine.h"
#include "heatmap_renderer.h"
#include <QWidget>
#include <QHash>
class QGridLayout;
class QTableWidget;
class QLabel;
class QComboBox;
class QSpinBox;
class MouseHeatmapTile;

class MouseStatistics : public QWidget {
public:
    explicit MouseStatistics(QWidget* parent=nullptr);
    void setStatistics(const wi::ViewResult& result);
    void clearRendering();
    QComboBox* softwareFilter()const{return softwareFilter_;}
private:
    QGridLayout* screens_;
    QTableWidget* ratios_;
    QLabel* empty_;
    QComboBox* buttonFilter_;
    QComboBox* softwareFilter_;
    QSpinBox* radius_;
    QComboBox* scaleMode_;
    QSpinBox* strength_;
    HeatmapRenderer* renderer_;
    std::shared_ptr<const HeatmapSources> sources_;
    std::shared_ptr<const HeatmapBatch> batch_;
    quint64 scope_=0;
    QHash<QString,MouseHeatmapTile*> tiles_;
    void openFullscreen(const wi::ViewResult::MouseScreen& screen);
    HeatmapSettings settings()const;
    void requestRendering();
    void showBatch(std::shared_ptr<const HeatmapBatch> batch);
};
