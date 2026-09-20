#include "device_illustrations.h"
#include "painting.h"

#include <QPainterPath>
#include <QRadialGradient>

namespace Ui
{
namespace
{
void paintCameraHousing(QPainter &p)
{
    QPainterPath side;
    side.moveTo(146, 20);
    side.lineTo(170, 34);
    side.lineTo(170, 148);
    side.lineTo(146, 160);
    side.closeSubpath();
    p.setPen(QPen(Theme::CameraDevice::SideBorder, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(linearGradient({146, 0}, {170, 0}, Theme::CameraDevice::Side));
    p.drawPath(side);
    panel(p, QRectF(10, 18, 144, 144), Theme::CameraDevice::Body);
    panel(p, QRectF(18, 26, 128, 128), Theme::CameraDevice::Face);
    for (const QPointF &point : {QPointF(29, 37), QPointF(135, 37), QPointF(29, 143), QPointF(135, 143)}) {
        p.setPen(QPen(Theme::CameraDevice::ScrewBorder, 1));
        p.setBrush(Theme::CameraDevice::ScrewFill);
        p.drawEllipse(point, 3.5, 3.5);
    }
}

void paintCameraLens(QPainter &p)
{
    const QPointF center(82, 90);
    p.setBrush(linearGradient({39, 45}, {124, 138}, Theme::CameraDevice::Rim));
    p.setPen(QPen(Theme::CameraDevice::RimBorder, 2));
    p.drawEllipse(center, 52, 52);
    p.setBrush(Theme::CameraDevice::InnerRim);
    p.setPen(QPen(Theme::CameraDevice::InnerRimBorder, 4));
    p.drawEllipse(center, 44, 44);
    QRadialGradient glass(center, 35, QPointF(72, 78));
    glass.setStops(Theme::CameraDevice::Glass);
    p.setBrush(glass);
    p.setPen(QPen(Theme::CameraDevice::GlassBorder, 4));
    p.drawEllipse(center, 35, 35);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::CameraDevice::Aperture);
    p.drawEllipse(center, 14, 14);
    p.setBrush(Theme::CameraDevice::Reflection);
    p.drawEllipse(QPointF(70, 73), 8, 5);
}
} // namespace

void cameraDevice(QPainter &p, const QRectF &rect)
{
    p.save();
    // 使用正方形画布等比缩放，保持机身比例及圆形镜头。
    const qreal scale = qMin(rect.width(), rect.height()) / 180.;
    p.translate(rect.center() - QPointF(90 * scale, 90 * scale));
    p.scale(scale, scale);
    p.setRenderHint(QPainter::Antialiasing);
    paintCameraHousing(p);
    paintCameraLens(p);
    p.restore();
}

void laptop(QPainter &p, const QRectF &rect)
{
    p.save();
    p.translate(rect.topLeft());
    p.scale(rect.width() / 240., rect.height() / 170.);
    panel(p, QRectF(24, 6, 194, 132), Theme::Laptop::Frame);
    panel(p, QRectF(30, 12, 182, 119), Theme::Laptop::Screen);
    p.setBrush(Theme::Laptop::Base);
    p.setPen(QPen(Theme::Laptop::BaseBorder, 3));
    p.drawPolygon(QPolygonF({{25, 139}, {216, 139}, {236, 155}, {232, 164}, {9, 164}, {4, 157}}));
    p.setBrush(Theme::Laptop::Keyboard);
    p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygonF({{37, 142}, {204, 142}, {215, 153}, {25, 153}}));
    p.setBrush(Theme::Laptop::Touchpad);
    p.drawRoundedRect(QRectF(96, 153, 48, 6), 2, 2);
    p.restore();
}
} // namespace Ui
