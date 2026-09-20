#pragma once

#include <QColor>
#include <QGradient>
#include <QString>

namespace Ui
{

struct PanelStyle {
    QColor top;
    QColor bottom;
    QColor border;
    qreal radius = 20;
};

// 所有颜色值集中在这里。按视觉用途命名，页面和绘制代码只引用样式。
namespace Theme
{
// Target images should ship Noto Sans CJK SC; Qt falls back to the platform
// default family when the font package is unavailable during host diagnostics.
extern const QString FontFamily;
extern const QColor TextPrimary;
extern const QColor TextMuted;
extern const QColor TextDark;
extern const PanelStyle DefaultPanel;

namespace Window
{
extern const QColor Canvas;
extern const QGradientStops Background;
extern const QColor PreviewBorder;
} // namespace Window

namespace Control
{
extern const QColor FocusOutline;
extern const QColor Feedback;
} // namespace Control

namespace Navigation
{
extern const QGradientStops Background;
extern const QColor Separator;
extern const PanelStyle SelectedTab;
extern const QColor Underline;
extern const QColor ActiveIcon;
extern const QColor InactiveIcon;
} // namespace Navigation

namespace Home
{
extern const QGradientStops Header;
extern const QColor HeaderShade;
extern const QGradientStops Background;
extern const QColor LeftWave;
extern const QColor RightWave;
extern const QColor Subtitle;
extern const PanelStyle Notice;
extern const QColor NoticeIcon;
extern const QColor NoticeDivider;
extern const QColor NoticeText;
extern const PanelStyle Card;
extern const QColor CardShadow;
extern const QColor CardPressedOutline;
extern const QColor CardDescription;
extern const PanelStyle CameraAccent;
extern const PanelStyle UvcAccent;
extern const PanelStyle OpenCvAccent;
} // namespace Home

namespace Overlay
{
extern const QColor Outline;
extern const QColor Background;
extern const QColor Icon;
extern const QColor SwitchOn;
extern const QColor SwitchOff;
extern const QColor SwitchKnob;
extern const QColor CaptureOutline;
extern const QColor CaptureInnerRing;
extern const QGradientStops Record;
extern const QColor RecordIndicator;
} // namespace Overlay

namespace Album
{
extern const PanelStyle Sidebar;
extern const QColor LibraryIcon;
extern const PanelStyle Filters;
extern const PanelStyle SelectedFilter;
extern const PanelStyle Preview;
extern const QColor EmptyText;
} // namespace Album

namespace Playback
{
extern const QGradientStops Play;
extern const QGradientStops Step;
extern const QColor PlayBorder;
extern const QColor StepBorder;
extern const QColor DisabledIcon;
} // namespace Playback

namespace Uvc
{
extern const PanelStyle Content;
extern const PanelStyle UsbLabel;
extern const PanelStyle EnableButton;
extern const PanelStyle StopButton;
extern const QColor ConnectedLine;
extern const QColor IdleLine;
extern const QColor EnabledIndicator;
extern const QColor IdleIndicator;
extern const QColor EnabledText;
extern const QColor IdleText;
extern const QColor Description;
extern const QColor StopOutline;
extern const QColor StopFill;
} // namespace Uvc

namespace Brand
{
extern const QGradientStops LogoLeft;
extern const QGradientStops LogoRight;
extern const QGradientStops LogoBase;
} // namespace Brand

namespace CameraDevice
{
extern const QGradientStops Side;
extern const QColor SideBorder;
extern const PanelStyle Body;
extern const PanelStyle Face;
extern const QColor ScrewBorder;
extern const QColor ScrewFill;
extern const QGradientStops Rim;
extern const QColor RimBorder;
extern const QColor InnerRim;
extern const QColor InnerRimBorder;
extern const QGradientStops Glass;
extern const QColor GlassBorder;
extern const QColor Aperture;
extern const QColor Reflection;
} // namespace CameraDevice

namespace Laptop
{
extern const PanelStyle Frame;
extern const PanelStyle Screen;
extern const QColor Base;
extern const QColor BaseBorder;
extern const QColor Keyboard;
extern const QColor Touchpad;
} // namespace Laptop
} // namespace Theme
} // namespace Ui
