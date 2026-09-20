#include "theme.h"

namespace Ui
{
namespace Theme
{
// Target images should ship Noto Sans CJK SC; Qt falls back to the platform
// default family when the font package is unavailable during host diagnostics.
const QString FontFamily = QStringLiteral("Noto Sans CJK SC");
const QColor TextPrimary("#f4f8ff");
const QColor TextMuted("#a9bedc");
const QColor TextDark("#071531");
const PanelStyle DefaultPanel{QColor("#14283f"), QColor("#0e1e30"), QColor("#294865")};

namespace Window
{
const QColor Canvas("#081525");
const QGradientStops Background{
    {0, QColor("#0d2139")}, {.5, QColor("#0a1728")}, {1, QColor("#06111f")}};
const QColor PreviewBorder("#496785");
} // namespace Window

namespace Control
{
const QColor FocusOutline("#66caff");
const QColor Feedback(100, 182, 255);
} // namespace Control

namespace Navigation
{
const QGradientStops Background{{0, QColor("#0d2139")}, {1, QColor("#081626")}};
const QColor Separator("#2b425d");
const PanelStyle SelectedTab{QColor("#133c74"), QColor("#075dcc"), QColor("#194579"), 17};
const QColor Underline("#25c9ff");
const QColor ActiveIcon("#55c7ff");
const QColor InactiveIcon("#8ba4c7");
} // namespace Navigation

namespace Home
{
const QGradientStops Header{
    {0, QColor("#123a70")}, {.35, QColor("#0e2546")}, {.67, QColor("#18375c")}, {1, QColor("#0c1c34")}};
const QColor HeaderShade(3, 15, 39, 65);
const QGradientStops Background{
    {0, QColor("#f2f9ff")}, {.5, QColor("#f4faff")}, {1, QColor("#e5f2ff")}};
const QColor LeftWave(103, 179, 252, 22);
const QColor RightWave(117, 192, 251, 24);
const QColor Subtitle("#546d93");
const PanelStyle Notice{QColor("#d8edff"), QColor("#d7eaff"), QColor("#d8edff"), 21};
const QColor NoticeIcon("#098bff");
const QColor NoticeDivider("#8eb5e6");
const QColor NoticeText("#41618d");
const PanelStyle Card{Qt::white, Qt::white, Qt::white, 25};
const QColor CardShadow(45, 65, 90);
const QColor CardPressedOutline("#219cff");
const QColor CardDescription("#4d648b");
const PanelStyle CameraAccent{QColor("#20acff"), QColor("#0066ff"), QColor(255, 255, 255, 80), 28};
const PanelStyle UvcAccent{QColor("#21c1c5"), QColor("#079796"), QColor(255, 255, 255, 80), 28};
const PanelStyle OpenCvAccent{QColor("#19c7b1"), QColor("#087f7a"), QColor(255, 255, 255, 80), 28};
} // namespace Home

namespace Overlay
{
const QColor Outline(220, 237, 255, 90);
const QColor Background(7, 18, 32);
const QColor Icon(220, 242, 255, 245);
const QColor SwitchOn(8, 125, 255, 220);
const QColor SwitchOff(202, 219, 236, 65);
const QColor SwitchKnob(255, 255, 255, 245);
const QColor CaptureOutline(245, 249, 255, 235);
const QColor CaptureInnerRing(255, 255, 255, 95);
const QGradientStops Record{{0, QColor(255, 85, 92, 225)}, {1, QColor(255, 41, 56, 205)}};
const QColor RecordIndicator("#ff3b30");
} // namespace Overlay

namespace Album
{
const PanelStyle Sidebar{QColor("#132947"), QColor("#09182c"), QColor("#2e517c"), 20};
const QColor LibraryIcon("#5cb9ff");
const PanelStyle Filters{QColor("#1b3557"), QColor("#152b49"), QColor("#42638f"), 22};
const PanelStyle SelectedFilter{QColor("#20b9ff"), QColor("#0670ff"), QColor("#439dff"), 20};
const PanelStyle Preview{Qt::black, Qt::black, QColor("#30465f"), 18};
const QColor EmptyText("#8899b0");
} // namespace Album

namespace Playback
{
const QGradientStops Play{{0, QColor("#24b8ff")}, {1, QColor("#0567ed")}};
const QGradientStops Step{{0, QColor("#1b304e")}, {1, QColor("#14243b")}};
const QColor PlayBorder("#6bd5ff");
const QColor StepBorder("#31557f");
const QColor DisabledIcon("#879ab6");
} // namespace Playback

namespace Uvc
{
const PanelStyle Content{QColor("#11222f"), QColor("#09141d"), QColor("#243b4d"), 25};
const PanelStyle UsbLabel{QColor("#263b49"), QColor("#162b39"), QColor("#2d4250"), 12};
const PanelStyle EnableButton{QColor("#155789"), QColor("#103c68"), QColor("#298bce"), 16};
const PanelStyle StopButton{QColor("#1d2933"), QColor("#15212b"), QColor("#3a4a57"), 16};
const QColor ConnectedLine("#2d97d8");
const QColor IdleLine("#455969");
const QColor EnabledIndicator("#31ca65");
const QColor IdleIndicator("#566d82");
const QColor EnabledText("#36d567");
const QColor IdleText("#a3b4c6");
const QColor Description("#a1b1c1");
const QColor StopOutline("#ff433e");
const QColor StopFill("#ff3d39");
} // namespace Uvc

namespace Brand
{
const QGradientStops LogoLeft{{0, QColor("#2ecef5")}, {.5, QColor("#008bff")}, {1, QColor("#1861d2")}};
const QGradientStops LogoRight{{0, QColor("#35d3f7")}, {1, QColor("#0084fd")}};
const QGradientStops LogoBase{{0, QColor("#2bcef6")}, {1, QColor("#027cff")}};
} // namespace Brand

namespace CameraDevice
{
const QGradientStops Side{{0, QColor("#29475c")}, {1, QColor("#112333")}};
const QColor SideBorder("#4d9dc9");
const PanelStyle Body{QColor("#355165"), QColor("#101c27"), QColor("#60b5e8"), 20};
const PanelStyle Face{QColor("#1c3040"), QColor("#0a151f"), QColor("#4a6578"), 14};
const QColor ScrewBorder("#657887");
const QColor ScrewFill("#141f29");
const QGradientStops Rim{
    {0, QColor("#7797aa")}, {.35, QColor("#263e50")}, {.8, QColor("#030a0f")}, {1, QColor("#657e8e")}};
const QColor RimBorder("#5283a3");
const QColor InnerRim("#060d15");
const QColor InnerRimBorder("#344f65");
const QGradientStops Glass{{0, QColor("#147ac7")}, {.45, QColor("#0a315c")}, {1, QColor("#020913")}};
const QColor GlassBorder("#1b3b55");
const QColor Aperture("#030c18");
const QColor Reflection(83, 174, 233, 110);
} // namespace CameraDevice

namespace Laptop
{
const PanelStyle Frame{QColor("#d2e1eb"), QColor("#667c91"), QColor("#d2e5f2"), 9};
const PanelStyle Screen{QColor("#111e2b"), QColor("#050b10"), QColor("#405467"), 4};
const QColor Base("#8298aa");
const QColor BaseBorder("#cfdfeb");
const QColor Keyboard("#233444");
const QColor Touchpad("#bad0de");
} // namespace Laptop
} // namespace Theme
} // namespace Ui
