#include "../dashboard.h"
#include "../ui/button_styles.h"
#include "../ui/device_illustrations.h"
#include "../ui/metrics.h"
#include "../ui/painting.h"

using namespace Ui;

namespace
{
void paintConnectionDiagram(QPainter &p, bool enabled)
{
    p.save();
    p.setOpacity(enabled ? 1. : .55);
    cameraDevice(p, QRectF(377, 287, 192, 216));
    laptop(p, QRectF(1001, 313, 240, 170));
    p.restore();
    p.setPen(QPen(enabled ? Theme::Uvc::ConnectedLine : Theme::Uvc::IdleLine, 4, Qt::DotLine, Qt::RoundCap));
    p.drawLine(QPointF(594, 389), QPointF(727, 389));
    p.drawLine(QPointF(872, 389), QPointF(993, 389));
    panel(p, QRectF(734, 355, 132, 69), Theme::Uvc::UsbLabel);
    text(p, QRectF(734, 355, 132, 69), QStringLiteral("USB"), 32, Theme::TextPrimary, false, Qt::AlignCenter);
}

void paintConnectionStatus(QPainter &p, bool enabled, const QString &status)
{
    p.setPen(Qt::NoPen);
    p.setBrush(enabled ? Theme::Uvc::EnabledIndicator : Theme::Uvc::IdleIndicator);
    p.drawEllipse(QPointF(684, 564), 20, 20);
    p.setPen(QPen(Theme::TextPrimary, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (enabled)
        p.drawPolyline(QPolygonF({{675, 564}, {682, 571}, {694, 557}}));
    else
        p.drawLine(QPointF(676, 564), QPointF(692, 564));
    text(p, QRectF(722, 536, 300, 57), enabled ? QStringLiteral("UVC 已启用") : QStringLiteral("UVC 未启用"),
         34, enabled ? Theme::Uvc::EnabledText : Theme::Uvc::IdleText, true);
    text(p, QRectF(300, 606, 1000, 82), status, 26, Theme::Uvc::Description,
         false, Qt::AlignCenter);
}
} // namespace

void Dashboard::createUvcPage()
{
    uvcToggleButton_ = button(QStringLiteral("uvcToggleButton"), QStringLiteral("启用 UVC"),
                              QRectF(569, 697, 462, 88), UvcPage,
                              ButtonStyles::uvcToggle());
    uvcToggleButton_->setCheckable(true);
    connect(uvcToggleButton_, &QAbstractButton::clicked, this, [this](bool checked) {
        uvcToggleButton_->setChecked(!checked);
        emit uvcToggleRequested(checked);
    });
}

void Dashboard::paintUvc(QPainter &p)
{
    text(p, QRectF(25, 13, 700, 64), QStringLiteral("UVC 输出"), 32, Theme::TextPrimary, true);
    panel(p, QRectF(272, 105, 1056, 802), Theme::Uvc::Content);
    p.save();
    p.translate(0, -Metrics::HeaderHeight / 2.);
    paintConnectionDiagram(p, uvcEnabled_);
    paintConnectionStatus(p, uvcEnabled_, uvcStatus_);
    p.restore();
}
