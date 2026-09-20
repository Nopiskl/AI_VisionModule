#include "painting.h"

namespace Ui
{
void text(QPainter &p, const QRectF &rect, const QString &value, int pixels, const QColor &color, bool bold,
          Qt::Alignment alignment)
{
    p.save();
    QFont font(Theme::FontFamily);
    font.setPixelSize(pixels);
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect, alignment, value);
    p.restore();
}

void panel(QPainter &p, const QRectF &rect, const PanelStyle &style)
{
    p.save();
    p.setBrush(linearGradient(rect.topLeft(), rect.bottomRight(), {{0, style.top}, {1, style.bottom}}));
    p.setPen(QPen(style.border, 1.5));
    p.drawRoundedRect(rect.adjusted(.8, .8, -.8, -.8), style.radius, style.radius);
    p.restore();
}

QLinearGradient linearGradient(const QPointF &start, const QPointF &end, const QGradientStops &stops)
{
    QLinearGradient gradient(start, end);
    gradient.setStops(stops);
    return gradient;
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}
} // namespace Ui
