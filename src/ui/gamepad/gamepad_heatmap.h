#pragma once
#include "engine/engine.h"
#include <QRectF>
#include <QString>
#include <QWidget>
#include <array>
class GamepadHeatmap : public QWidget {
  public:
    explicit GamepadHeatmap(QWidget *parent = nullptr);
    void setStatistics(const wi::ViewResult &result);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

  private:
    struct Control {
        QString id, label;
        QRectF rect;
        bool round = false;
        wi::Count count = 0;
    };
    std::vector<Control> controls_;
    std::array<std::array<wi::Count, 16>, 2> directions_{};
    wi::Count total_ = 0, maximum_ = 0;
    QRectF area() const;
    QRectF mapped(const QRectF &bounds) const;
};
