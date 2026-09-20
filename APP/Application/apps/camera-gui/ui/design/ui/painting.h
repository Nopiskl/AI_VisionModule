#pragma once

#include "theme.h"
#include <QLinearGradient>
#include <QPainter>

namespace Ui
{
void text(QPainter &p, const QRectF &rect, const QString &value, int pixels,
          const QColor &color = Theme::TextPrimary, bool bold = false,
          Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter);
void panel(QPainter &p, const QRectF &rect, const PanelStyle &style = Theme::DefaultPanel);
QLinearGradient linearGradient(const QPointF &start, const QPointF &end, const QGradientStops &stops);
QColor withAlpha(QColor color, int alpha);
} // namespace Ui
