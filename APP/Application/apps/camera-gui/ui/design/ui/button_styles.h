#pragma once

#include "canvas_button.h"
#include "icons.h"

namespace Ui
{
namespace ButtonStyles
{
CanvasButton::Paint modeCard(Icon type, const QString &title, const QString &description);
CanvasButton::Paint navigationTab(Icon type, const QString &title);
CanvasButton::Paint back();
CanvasButton::Paint exitApplication();
CanvasButton::Paint overlayToggle(Icon type, const QString &label);
CanvasButton::Paint photoCapture();
CanvasButton::Paint recordCapture();
CanvasButton::Paint iosRecordingStop();
CanvasButton::Paint mediaFilter(Icon type, const QString &label);
CanvasButton::Paint playback(Icon type, qreal size, bool primary);
CanvasButton::Paint uvcToggle();
} // namespace ButtonStyles
} // namespace Ui
