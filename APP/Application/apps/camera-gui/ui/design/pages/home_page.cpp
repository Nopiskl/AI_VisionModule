#include "../dashboard.h"
#include "../ui/button_styles.h"
#include "../ui/metrics.h"
#include "../ui/painting.h"

#include <QPainterPath>
#include <QFontMetrics>

using namespace Ui;
using namespace Ui::Metrics;

namespace
{
void paintBrandHeader(QPainter &p)
{
    p.fillRect(QRectF(0, 0, DesignWidth, HeaderHeight),
               linearGradient({0, 0}, {1500, 110}, Theme::Home::Header));
    p.setBrush(Theme::Home::HeaderShade);
    p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygonF({{510, 0}, {610, 0}, {552, 98}, {452, 98}}));
    icon(p, Icon::Logo, QRectF(48, 20, 62, 61));
    text(p, QRectF(135, 8, 700, 83), QStringLiteral("AI Vision Module"), 43, Theme::TextPrimary, true);
}

void paintHomeBackground(QPainter &p)
{
    p.fillRect(QRectF(0, 98, 1600, 862), linearGradient({200, 120}, {1400, 960}, Theme::Home::Background));
    QPainterPath left;
    left.moveTo(0, 98);
    left.lineTo(426, 98);
    left.cubicTo(355, 222, 201, 303, 0, 360);
    left.closeSubpath();
    p.fillPath(left, Theme::Home::LeftWave);
    QPainterPath right;
    right.moveTo(1600, 170);
    right.cubicTo(1220, 197, 1321, 306, 1100, 368);
    right.lineTo(1600, 561);
    right.closeSubpath();
    p.fillPath(right, Theme::Home::RightWave);
}

void paintModeNotice(QPainter &p, const QString &summary)
{
    panel(p, QRectF(50, 867, 1500, 68), Theme::Home::Notice);
    icon(p, Icon::Info, QRectF(540, 880, 42, 42), Theme::Home::NoticeIcon);
    p.setPen(QPen(Theme::Home::NoticeDivider, 2));
    p.drawLine(QPointF(608, 884), QPointF(608, 918));
    text(p, QRectF(632, 869, 850, 65), summary, 24,
         Theme::Home::NoticeText);
}
} // namespace

void Dashboard::createHomePage()
{
    auto *exitButton = button(QStringLiteral("exitApplicationButton"),
                              QStringLiteral("退出程序"),
                              QRectF(1360, 14, 220, 68), HomePage,
                              ButtonStyles::exitApplication());
    connect(exitButton, &QAbstractButton::clicked, this,
            &Dashboard::exitApplicationRequested);
    cameraModeButton_ =
        button(QStringLiteral("cameraModeButton"), QStringLiteral("进入相机模式"),
               QRectF(50, 328, 480, 514), HomePage,
               ButtonStyles::modeCard(Icon::Camera, QStringLiteral("相机模式"),
                                      QStringLiteral("实时预览、拍照、录像、\n支持 RTSP 推流")));
    uvcModeButton_ =
        button(QStringLiteral("uvcModeButton"), QStringLiteral("进入 UVC 输出"),
               QRectF(560, 328, 480, 514), HomePage,
               ButtonStyles::modeCard(Icon::Usb, QStringLiteral("UVC 输出"),
                                      QStringLiteral("作为 USB 摄像头输出，\n供其他设备使用")));
    openCvModeButton_ =
        button(QStringLiteral("opencvModeButton"), QStringLiteral("OpenCV 采集"),
               QRectF(1070, 328, 480, 514), HomePage,
               ButtonStyles::modeCard(Icon::Code, QStringLiteral("OpenCV 采集"),
                                      QStringLiteral("启动独立 YOLO 采集\n退出后返回本界面")));
    connect(cameraModeButton_, &QAbstractButton::clicked, this, [this] {
        setPage(Page::Camera);
        emit cameraPageRequested();
    });
    connect(uvcModeButton_, &QAbstractButton::clicked, this, [this] {
        setPage(Page::Uvc);
        emit uvcPageRequested();
    });
    connect(openCvModeButton_, &QAbstractButton::clicked, this,
            &Dashboard::openCvSessionRequested);
}

void Dashboard::paintHome(QPainter &p)
{
    paintBrandHeader(p);
    if (!statusMessage_.isEmpty()) {
        QFont statusFont(Theme::FontFamily);
        statusFont.setPixelSize(22);
        text(p, QRectF(780, 14, 550, 68),
             QFontMetrics(statusFont).elidedText(statusMessage_, Qt::ElideRight, 550), 22,
             Theme::TextPrimary, false, Qt::AlignRight | Qt::AlignVCenter);
    }
    QPainterPath body;
    body.addRoundedRect(QRectF(0, 98, 1600, 888), 26, 26);
    p.save();
    p.setClipPath(body);
    paintHomeBackground(p);
    text(p, QRectF(0, 137, 1600, 99), QStringLiteral("选择工作模式"), 65, Theme::TextDark, true,
         Qt::AlignCenter);
    text(p, QRectF(50, 243, 1500, 53), QStringLiteral("根据您的需求选择对应的功能，工作模式互斥使用"), 30,
         Theme::Home::Subtitle, false, Qt::AlignCenter);
    paintModeNotice(p, homeModeSummary_);
    p.restore();
}
