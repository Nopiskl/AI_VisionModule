#pragma once

#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

namespace camera {
namespace gui {

// Must match the overlay's linuxfb rotation parameter. Other QPA backends
// do not apply this software rotation.
inline int linuxfbRotation(const QString& platform) {
    const auto parts = platform.split(QLatin1Char(':'));
    if (parts.isEmpty() || parts.front() != QStringLiteral("linuxfb"))
        return 0;
    int rotation = 0;
    for (const auto& part : parts) {
        if (part == QStringLiteral("rotation=0")) rotation = 0;
        else if (part == QStringLiteral("rotation=90")) rotation = 90;
        else if (part == QStringLiteral("rotation=180")) rotation = 180;
        else if (part == QStringLiteral("rotation=270")) rotation = 270;
    }
    return rotation;
}

inline QSize logicalDisplaySize(QSize physical, int rotation) {
    return rotation == 90 || rotation == 270
               ? QSize(physical.height(), physical.width()) : physical;
}

// Rectangles use pixel edges, not inclusive integer QRect right()/bottom().
// Invert the QPainter transform in the supplied linuxfb rotation patch.
inline QRectF physicalToLogicalRect(const QRectF& rect, QSize physical,
                                    int rotation) {
    if (!physical.isValid() || !rect.isValid() ||
        !QRectF(0, 0, physical.width(), physical.height()).contains(rect))
        return {};
    const qreal x = rect.x(), y = rect.y();
    const qreal w = rect.width(), h = rect.height();
    switch (rotation) {
    case 0: return rect;
    case 90: return {y, physical.width() - x - w, h, w};
    case 180:
        return {physical.width() - x - w, physical.height() - y - h, w, h};
    case 270: return {physical.height() - y - h, x, h, w};
    default: return {};
    }
}

} // namespace gui
} // namespace camera
