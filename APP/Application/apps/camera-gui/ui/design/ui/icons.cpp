#include "icons.h"
#include "painting.h"

#include <QPainterPath>

namespace Ui
{
namespace
{
void paintLogo(QPainter &p)
{
    p.setPen(Qt::NoPen);
    const auto a = linearGradient({15, 90}, {60, 5}, Theme::Brand::LogoLeft);
    p.setBrush(a);
    QPainterPath left;
    left.moveTo(39, 13);
    left.quadTo(48, -1, 59, 12);
    left.lineTo(76, 41);
    left.lineTo(54, 42);
    left.lineTo(24, 94);
    left.lineTo(11, 94);
    left.quadTo(-1, 93, 6, 78);
    left.closeSubpath();
    p.drawPath(left);
    const auto b = linearGradient({49, 8}, {94, 88}, Theme::Brand::LogoRight);
    p.setBrush(b);
    QPainterPath right;
    right.moveTo(43, 21);
    right.quadTo(37, 7, 50, 5);
    right.quadTo(59, 3, 64, 15);
    right.lineTo(95, 79);
    right.quadTo(103, 96, 86, 96);
    right.quadTo(78, 96, 75, 87);
    right.closeSubpath();
    p.drawPath(right);
    const auto c = linearGradient({16, 80}, {65, 91}, Theme::Brand::LogoBase);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(5, 72, 61, 24), 12, 12);
}

void paintCameraGlyph(QPainter &p, const QColor &color)
{
    QPainterPath body;
    body.moveTo(13, 27);
    body.lineTo(28, 27);
    body.lineTo(35, 14);
    body.quadTo(37, 11, 43, 11);
    body.lineTo(61, 11);
    body.quadTo(66, 11, 68, 17);
    body.lineTo(73, 27);
    body.lineTo(87, 27);
    body.quadTo(97, 27, 97, 38);
    body.lineTo(97, 79);
    body.quadTo(97, 90, 86, 90);
    body.lineTo(13, 90);
    body.quadTo(3, 90, 3, 79);
    body.lineTo(3, 38);
    body.quadTo(3, 27, 13, 27);
    QPainterPath lens;
    lens.addEllipse(QPointF(50, 55), 22, 22);
    lens.addEllipse(QPointF(82, 38), 3.5, 3.5);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(body.subtracted(lens));
    p.drawEllipse(QPointF(50, 55), 14, 14);
}

void paintUsbGlyph(QPainter &p, const QColor &color)
{
    p.setPen(QPen(color, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(49, 79), QPointF(49, 15));
    QPainterPath branch;
    branch.moveTo(49, 71);
    branch.lineTo(29, 58);
    branch.quadTo(26, 55, 26, 49);
    branch.lineTo(26, 39);
    p.drawPath(branch);
    QPainterPath right;
    right.moveTo(49, 60);
    right.lineTo(70, 47);
    right.lineTo(70, 30);
    p.drawPath(right);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(49, 85), 11, 11);
    p.drawEllipse(QPointF(26, 35), 9, 9);
    p.drawRoundedRect(QRectF(62, 20, 16, 17), 2, 2);
    QPainterPath arrow;
    arrow.moveTo(39, 16);
    arrow.lineTo(49, 1);
    arrow.lineTo(59, 16);
    arrow.closeSubpath();
    p.drawPath(arrow);
}

void paintFrameCorners(QPainter &p, Icon type, const QColor &color)
{
    const qreal a = type == Icon::Expand ? 21 : 10;
    const qreal b = 100 - a;
    p.drawPolyline(QPolygonF({{a, a + 18}, {a, a}, {a + 18, a}}));
    p.drawPolyline(QPolygonF({{b - 18, a}, {b, a}, {b, a + 18}}));
    p.drawPolyline(QPolygonF({{a, b - 18}, {a, b}, {a + 18, b}}));
    p.drawPolyline(QPolygonF({{b - 18, b}, {b, b}, {b, b - 18}}));
    if (type == Icon::Resolution) {
        p.setPen(QPen(color, 5, Qt::DotLine));
        p.drawLine(QPointF(40, a), QPointF(61, a));
        p.drawLine(QPointF(40, b), QPointF(61, b));
    }
}

void paintMediaStep(QPainter &p, Icon type, const QColor &color)
{
    if (type == Icon::Next) {
        p.translate(100, 0);
        p.scale(-1, 1);
    }
    p.setPen(QPen(color, 9, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(24, 23), QPointF(24, 77));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF({{76, 18}, {30, 50}, {76, 82}}));
}
} // namespace

void icon(QPainter &p, Icon type, const QRectF &rect, const QColor &color)
{
    p.save();
    p.translate(rect.topLeft());
    p.scale(rect.width() / 100., rect.height() / 100.);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (type) {
    case Icon::Logo:
        paintLogo(p);
        break;
    case Icon::Camera:
        paintCameraGlyph(p, color);
        break;
    case Icon::Usb:
        paintUsbGlyph(p, color);
        break;
    case Icon::Code:
        p.setPen(QPen(color, 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({{25, 29}, {6, 50}, {25, 71}}));
        p.drawPolyline(QPolygonF({{76, 29}, {95, 50}, {76, 71}}));
        p.drawLine(QPointF(59, 18), QPointF(43, 83));
        break;
    case Icon::Photo:
        p.drawRoundedRect(QRectF(8, 10, 84, 80), 7, 7);
        p.drawPolyline(QPolygonF({{10, 74}, {33, 48}, {51, 64}, {69, 38}, {91, 69}}));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(30, 30), 7, 7);
        break;
    case Icon::Video:
        p.drawRoundedRect(QRectF(7, 24, 61, 55), 8, 8);
        p.drawPolygon(QPolygonF({{69, 39}, {93, 25}, {93, 78}, {69, 65}}));
        break;
    case Icon::Resolution:
    case Icon::Expand:
        paintFrameCorners(p, type, color);
        break;
    case Icon::Document:
        p.drawPolygon(QPolygonF({{21, 8}, {61, 8}, {83, 32}, {83, 91}, {21, 91}}));
        p.drawPolyline(QPolygonF({{60, 10}, {60, 34}, {82, 34}}));
        p.drawLine(QPointF(36, 55), QPointF(64, 55));
        p.drawLine(QPointF(36, 69), QPointF(57, 69));
        break;
    case Icon::Network:
        p.drawEllipse(QRectF(40, 4, 20, 20));
        p.drawLine(QPointF(50, 26), QPointF(50, 47));
        p.drawPolyline(QPolygonF({{15, 65}, {15, 47}, {85, 47}, {85, 65}}));
        p.drawLine(QPointF(50, 49), QPointF(50, 65));
        for (int x : {5, 40, 75})
            p.drawRoundedRect(QRectF(x, 66, 20, 23), 5, 5);
        break;
    case Icon::Detect:
        icon(p, Icon::Expand, QRectF(-15, -15, 130, 130), color);
        p.drawEllipse(QPointF(50, 50), 24, 24);
        p.drawEllipse(QPointF(50, 50), 7, 7);
        break;
    case Icon::Back:
        p.drawPolyline(QPolygonF({{60, 20}, {29, 50}, {60, 80}}));
        break;
    case Icon::Chevron:
        p.drawPolyline(QPolygonF({{36, 18}, {66, 50}, {36, 82}}));
        break;
    case Icon::Play:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF({{28, 12}, {85, 50}, {28, 88}}));
        break;
    case Icon::Previous:
    case Icon::Next:
        paintMediaStep(p, type, color);
        break;
    case Icon::Trash:
        p.drawRoundedRect(QRectF(25, 31, 51, 59), 4, 4);
        p.drawLine(QPointF(17, 25), QPointF(84, 25));
        p.drawPolyline(QPolygonF({{38, 24}, {38, 12}, {63, 12}, {63, 24}}));
        p.drawLine(QPointF(43, 43), QPointF(43, 77));
        p.drawLine(QPointF(59, 43), QPointF(59, 77));
        break;
    case Icon::Info:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(3, 3, 94, 94));
        text(p, QRectF(0, -3, 100, 100), QStringLiteral("i"), 74, Qt::white, true, Qt::AlignCenter);
        break;
    }
    p.restore();
}

} // namespace Ui
