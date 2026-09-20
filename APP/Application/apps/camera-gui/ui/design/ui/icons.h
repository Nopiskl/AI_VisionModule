#pragma once

#include "theme.h"
#include <QPainter>

namespace Ui
{
enum class Icon {
    Logo,
    Camera,
    Usb,
    Code,
    Photo,
    Video,
    Resolution,
    Document,
    Network,
    Detect,
    Back,
    Chevron,
    Expand,
    Play,
    Previous,
    Next,
    Trash,
    Info
};
void icon(QPainter &p, Icon type, const QRectF &rect, const QColor &color = Theme::TextPrimary);
} // namespace Ui
