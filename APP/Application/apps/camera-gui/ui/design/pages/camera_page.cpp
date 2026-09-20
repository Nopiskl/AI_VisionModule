#include "../dashboard.h"
#include "../ui/button_styles.h"
#include "../ui/metrics.h"
#include "../ui/painting.h"

using namespace Ui;
using namespace Ui::Metrics;

void Dashboard::createCameraPage()
{
    auto *preview = new QWidget(this);
    preview->setObjectName(QStringLiteral("cameraPreview"));
    preview->setAccessibleName(QStringLiteral("透明摄像机预览"));
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    preview->setAttribute(Qt::WA_NoSystemBackground);
    preview->setAutoFillBackground(false);
    place(preview, QRectF(0, NavigationHeight, DesignWidth, DesignHeight - NavigationHeight), CameraPage);

    auto addToggle = [this](const QString &name, const QString &label, Icon type, qreal x) {
        auto *toggle = button(name, label, QRectF(x, 110, 264, 64), CameraPage,
                              ButtonStyles::overlayToggle(type, label));
        toggle->setCheckable(true);
        toggle->setChecked(false);
        return toggle;
    };
    rtspToggle_ = addToggle(QStringLiteral("rtspToggle"), QStringLiteral("RTSP 推流"),
                            Icon::Network, 1032);
    detectionToggle_ = addToggle(QStringLiteral("detectionToggle"), QStringLiteral("实时检测"),
                                 Icon::Detect, 1312);
    detectionToggle_->setEnabled(false);
    detectionToggle_->setToolTip(QStringLiteral("实时检测需切换到首页 OpenCV 模式"));

    photoButton_ = button(QStringLiteral("photoButton"), QStringLiteral("拍照"),
                          QRectF(692, 848, 88, 88), CameraPage,
                          ButtonStyles::photoCapture());
    recordButton_ = button(QStringLiteral("recordButton"), QStringLiteral("录像"),
                           QRectF(820, 848, 88, 88), CameraPage,
                           ButtonStyles::recordCapture());
    recordButton_->setCheckable(true);
    recordStopButton_ = button(QStringLiteral("recordStopButton"), QStringLiteral("停止录像"),
                               QRectF(744, 824, 112, 112), CameraPage,
                               ButtonStyles::iosRecordingStop());
    recordStopButton_->setVisible(false);

    connect(photoButton_, &QAbstractButton::clicked, this, &Dashboard::snapshotRequested);
    connect(recordButton_, &QAbstractButton::clicked, this, [this] {
        recordButton_->setChecked(!recordButton_->isChecked());
        emit recordToggleRequested();
    });
    connect(recordStopButton_, &QAbstractButton::clicked, this,
            &Dashboard::recordToggleRequested);
    connect(rtspToggle_, &QAbstractButton::clicked, this, [this](bool checked) {
        rtspToggle_->setChecked(!checked);
        emit rtspToggleRequested(checked);
    });
}

void Dashboard::paintCamera(QPainter &p)
{
    // 内容区由 Dashboard 清为透明；此处只绘制顶部导航栏。
    p.fillRect(QRectF(0, 0, DesignWidth, NavigationHeight),
               linearGradient({0, 0}, {DesignWidth, NavigationHeight}, Theme::Navigation::Background));
    p.setPen(QPen(Theme::Navigation::Separator, 1.5));
    p.drawLine(QPointF(24, 71), QPointF(1576, 71));
    if (cameraRecording_) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Overlay::RecordIndicator);
        p.drawEllipse(QPointF(684, 36), 9, 9);
        text(p, QRectF(708, 5, 340, 62),
             QStringLiteral("已录制 %1").arg(cameraRecordElapsed_), 27,
             Theme::TextPrimary, true, Qt::AlignVCenter | Qt::AlignLeft);
        return;
    }
    text(p, QRectF(500, 5, 850, 62), cameraRtspStatus_, 23, Theme::TextPrimary,
         false, Qt::AlignVCenter | Qt::AlignRight);
    if (!statusMessage_.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(7, 18, 32, 125));
        p.drawRoundedRect(QRectF(22, 878, 610, 48), 18, 18);
        text(p, QRectF(42, 878, 570, 48), statusMessage_, 21,
             Theme::TextPrimary, false, Qt::AlignVCenter | Qt::AlignLeft);
    }
}
