#pragma once

#include <QAbstractButton>
#include <QPainter>
#include <functional>

namespace Ui
{
// 负责输入、焦点和缩放；具体外观由 button_styles 中的绘制函数提供。
class CanvasButton final : public QAbstractButton
{
public:
    using Paint = std::function<void(QPainter &, const CanvasButton &)>;
    CanvasButton(const QSizeF &designSize, Paint paint, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *event) override;

private:
    QSizeF designSize_;
    Paint paint_;
};
} // namespace Ui
