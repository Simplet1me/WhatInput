#pragma once
#include "engine/engine.h"
#include <QColor>
#include <QRectF>
#include <QStringList>
#include <QWidget>

class KeyboardHeatmap : public QWidget {
  public:
    explicit KeyboardHeatmap(QWidget *parent = nullptr);
    void setStatistics(const wi::ViewResult &result);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

  private:
    struct Key {
        QString label;
        QStringList controls;
        QRectF bounds;
        wi::Count count = 0;
    };
    std::vector<Key> keys_;
    wi::Count total_ = 0, maximum_ = 0;
    int hovered_ = -1;
    QRectF screenBounds(const Key &key) const;
    QColor color(wi::Count count) const;
    void add(QString label, unsigned scan, unsigned flags, double x, double y,
             double width = 1, double height = 1);
};
