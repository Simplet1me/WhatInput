#include "ui/keyboard/keyboard_heatmap.h"
#include <QLocale>
#include "ui/heat_scale.h"
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

void KeyboardHeatmap::add(QString label, unsigned scan, unsigned flags,
                          double x, double y, double width, double height) {
    const auto code =
        QString("kbd:%1:%2").arg(scan, 4, 16, QChar('0')).arg(flags).toUpper();
    // The stored control prefix is lowercase; only the hexadecimal digits are
    // uppercase.
    keys_.push_back({std::move(label),
                     {QString("kbd:") + code.mid(4)},
                     {x, y, width, height},
                     0});
}
KeyboardHeatmap::KeyboardHeatmap(QWidget *parent) : QWidget(parent) {
    setMinimumSize(740, 280);
    setMouseTracking(true);
    setAccessibleName("104 键标准键盘输入热力图");
    add("Esc", 0x01, 0, 0, 0);
    for (unsigned i = 0; i < 12; ++i) {
        const double x = i < 4 ? 2 + i : i < 8 ? 2.5 + i : 3 + i;
        add("F" + QString::number(i + 1), i < 10 ? 0x3b + i : 0x57 + i - 10, 0,
            x, 0);
    }
    add("PrtSc", 0x37, 2, 15.6, 0);
    keys_.back().controls << "kbd:0054:0";
    add("ScrLk", 0x46, 0, 16.6, 0);
    add("Pause", 0x45, 4, 17.6, 0);
    keys_.back().controls << "kbd:001D:4";
    const double y1 = 1.25, y2 = 2.25, y3 = 3.25, y4 = 4.25, y5 = 5.25;
    add("`", 0x29, 0, 0, y1);
    for (unsigned i = 0; i < 10; ++i)
        add(QString::number((i + 1) % 10), 0x02 + i, 0, 1 + i, y1);
    add("-", 0x0c, 0, 11, y1);
    add("=", 0x0d, 0, 12, y1);
    add("Backspace", 0x0e, 0, 13, y1, 2);
    add("Tab", 0x0f, 0, 0, y2, 1.5);
    const QString q = "QWERTYUIOP";
    for (int i = 0; i < q.size(); ++i)
        add(QString(q[i]), 0x10 + i, 0, 1.5 + i, y2);
    add("[", 0x1a, 0, 11.5, y2);
    add("]", 0x1b, 0, 12.5, y2);
    add("\\", 0x2b, 0, 13.5, y2, 1.5);
    add("Caps", 0x3a, 0, 0, y3, 1.75);
    const QString a = "ASDFGHJKL";
    for (int i = 0; i < a.size(); ++i)
        add(QString(a[i]), 0x1e + i, 0, 1.75 + i, y3);
    add(";", 0x27, 0, 10.75, y3);
    add("'", 0x28, 0, 11.75, y3);
    add("Enter", 0x1c, 0, 12.75, y3, 2.25);
    add("Shift", 0x2a, 0, 0, y4, 2.25);
    const QString z = "ZXCVBNM";
    for (int i = 0; i < z.size(); ++i)
        add(QString(z[i]), 0x2c + i, 0, 2.25 + i, y4);
    add(",", 0x33, 0, 9.25, y4);
    add(".", 0x34, 0, 10.25, y4);
    add("/", 0x35, 0, 11.25, y4);
    add("Shift", 0x36, 0, 12.25, y4, 2.75);
    add("Ctrl", 0x1d, 0, 0, y5, 1.25);
    add("Win", 0x5b, 2, 1.25, y5, 1.25);
    add("Alt", 0x38, 0, 2.5, y5, 1.25);
    add("Space", 0x39, 0, 3.75, y5, 6.25);
    add("Alt", 0x38, 2, 10, y5, 1.25);
    add("Win", 0x5c, 2, 11.25, y5, 1.25);
    add("Menu", 0x5d, 2, 12.5, y5, 1.25);
    add("Ctrl", 0x1d, 2, 13.75, y5, 1.25);
    add("Ins", 0x52, 2, 15.6, y1);
    add("Home", 0x47, 2, 16.6, y1);
    add("PgUp", 0x49, 2, 17.6, y1);
    add("Del", 0x53, 2, 15.6, y2);
    add("End", 0x4f, 2, 16.6, y2);
    add("PgDn", 0x51, 2, 17.6, y2);
    add("↑", 0x48, 2, 16.6, y4);
    add("←", 0x4b, 2, 15.6, y5);
    add("↓", 0x50, 2, 16.6, y5);
    add("→", 0x4d, 2, 17.6, y5);
    add("Num", 0x45, 0, 19.2, y1);
    keys_.back().controls << "kbd:0045:2";
    add("/", 0x35, 2, 20.2, y1);
    add("*", 0x37, 0, 21.2, y1);
    add("-", 0x4a, 0, 22.2, y1);
    add("7", 0x47, 0, 19.2, y2);
    add("8", 0x48, 0, 20.2, y2);
    add("9", 0x49, 0, 21.2, y2);
    add("+", 0x4e, 0, 22.2, y2, 1, 2);
    add("4", 0x4b, 0, 19.2, y3);
    add("5", 0x4c, 0, 20.2, y3);
    add("6", 0x4d, 0, 21.2, y3);
    add("1", 0x4f, 0, 19.2, y4);
    add("2", 0x50, 0, 20.2, y4);
    add("3", 0x51, 0, 21.2, y4);
    add("Enter", 0x1c, 2, 22.2, y4, 1, 2);
    add("0", 0x52, 0, 19.2, y5, 2);
    add(".", 0x53, 0, 21.2, y5);
}
void KeyboardHeatmap::setStatistics(const wi::ViewResult &result) {
    total_ = result.total;
    maximum_ = 0;
    for (auto &key : keys_) {
        key.count = 0;
        for (const auto &row : result.controls)
            if (row.type == "keyboard" &&
                key.controls.contains(QString::fromStdString(row.control)))
                key.count += row.count;
        maximum_ = std::max(maximum_, key.count);
    }
    update();
}
QRectF KeyboardHeatmap::screenBounds(const Key &key) const {
    const double unit = std::min((width() - 24) / 23.2, (height() - 70) / 6.25);
    const double left = (width() - 23.2 * unit) / 2,
                 top = 50 + std::max(0.0,(height() - 70 - 6.25 * unit) / 4);
    return {left + key.bounds.x() * unit, top + key.bounds.y() * unit,
            key.bounds.width() * unit - 4, key.bounds.height() * unit - 4};
}
QColor KeyboardHeatmap::color(wi::Count count) const {
    return heatColor(maximum_?double(count)/maximum_:0);
}
void KeyboardHeatmap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#17232e"));
    paintHeatLegend(painter,width(),double(maximum_));
    const double unit = std::min((width() - 24) / 23.2, (height() - 70) / 6.25);
    for (int i = 0; i < int(keys_.size()); ++i) {
        const auto &key = keys_[i];
        const auto area = screenBounds(key);
        painter.setPen(
            QPen(i == hovered_ ? QColor("#cceade") : QColor("#434453"),
                 i == hovered_ ? 1.6 : 1));
        painter.setBrush(color(key.count));
        painter.drawRoundedRect(area, 4, 4);
        auto font = painter.font();
        font.setPixelSize(std::clamp(
            int(unit * (key.label.size() > 5 ? 0.26 : 0.32)), 9, 18));
        painter.setFont(font);
        painter.setPen(QColor("#e4e4ed"));
        painter.drawText(area.adjusted(2, 2, -2, -2), Qt::AlignCenter,
                         key.label);
    }
}
void KeyboardHeatmap::mouseMoveEvent(QMouseEvent *event) {
    int found = -1;
    for (int i = 0; i < int(keys_.size()); ++i)
        if (screenBounds(keys_[i]).contains(event->position())) {
            found = i;
            break;
        }
    if (found != hovered_) {
        hovered_ = found;
        update();
    }
    if (found < 0) {
        QToolTip::hideText();
        return;
    }
    const auto &key = keys_[found];
    const auto share = total_ ? double(key.count) * 100 / double(total_) : 0;
    QString name = key.label;
    if (name == "Ctrl" || name == "Alt" || name == "Win")
        name = (key.bounds.x() < 4 ? "左 " : "右 ") + name;
    if (name == "Shift")
        name = (key.bounds.x() < 4 ? "左 " : "右 ") + name;
    if (name == "Enter" && key.bounds.x() > 19)
        name = "小键盘 Enter";
    QToolTip::showText(
        event->globalPosition().toPoint(),
        name + "\n" + QLocale(QLocale::Chinese).toString(qlonglong(key.count)) +
            " 次 · " + QString::number(share, 'f', 2) + "%",
        this);
}
void KeyboardHeatmap::leaveEvent(QEvent *event) {
    hovered_ = -1;
    update();
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}
