#include "canvas_button.h"
#include "theme.h"

#include <QEvent>
#include <utility>

namespace Ui
{
CanvasButton::CanvasButton(const QSizeF &designSize, Paint paint, QWidget *parent)
    : QAbstractButton(parent), designSize_(designSize), paint_(std::move(paint))
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void CanvasButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(width() / designSize_.width(), height() / designSize_.height());
    paint_(p, *this);
    if (hasFocus() && isEnabled()) {
        p.setPen(QPen(Theme::Control::FocusOutline, 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(QPointF(5, 5), designSize_ - QSizeF(10, 10)), 12, 12);
    }
}

bool CanvasButton::event(QEvent *event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        update();
    return QAbstractButton::event(event);
}
} // namespace Ui
