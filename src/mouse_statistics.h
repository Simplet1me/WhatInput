#pragma once
#include "engine.h"
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
private:
    QGridLayout* screens_;
    QTableWidget* ratios_;
    QLabel* empty_;
    QComboBox* buttonFilter_;
    QSpinBox* radius_;
    QHash<QString,MouseHeatmapTile*> tiles_;
    void openFullscreen(const wi::ViewResult::MouseScreen& screen);
};
