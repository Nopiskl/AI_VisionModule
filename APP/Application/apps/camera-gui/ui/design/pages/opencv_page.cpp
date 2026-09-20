#include "../dashboard.h"
#include "../ui/painting.h"

#include <QPainterPath>

using namespace Ui;

namespace
{
const QRectF PreviewRect(24, 78, 1552, 866);
}

void Dashboard::createOpenCvPage()
{
    // 保留命令行预览占位页；主页卡片不导航到这里。
    auto *preview = new QWidget(this);
    preview->setObjectName(QStringLiteral("opencvPreview"));
    preview->setAccessibleName(QStringLiteral("OpenCV 透明预览"));
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    preview->setAttribute(Qt::WA_NoSystemBackground);
    place(preview, PreviewRect, OpenCvPage);
}

void Dashboard::paintOpenCv(QPainter &p)
{
    text(p, QRectF(25, 6, 700, 64), QStringLiteral("OpenCV 采集"), 32, Theme::TextPrimary, true);
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    QPainterPath hole;
    hole.addRoundedRect(PreviewRect, 18, 18);
    p.fillPath(hole, Qt::transparent);
    p.restore();
    p.setPen(QPen(Theme::Window::PreviewBorder, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(PreviewRect, 18, 18);
}
