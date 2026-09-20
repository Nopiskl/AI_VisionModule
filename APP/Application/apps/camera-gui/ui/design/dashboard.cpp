#include "dashboard.h"
#include "ui/icons.h"
#include "ui/metrics.h"
#include "ui/painting.h"

#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QWindow>
#include <utility>

using namespace Ui;
using namespace Ui::Metrics;

Dashboard::Dashboard(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("AIVisionModule"));
    setWindowTitle(QStringLiteral("AI Vision Module"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(DefaultWidth, DefaultHeight);
    setFocusPolicy(Qt::ClickFocus);
    QPixmap logo(64, 64);
    logo.fill(Qt::transparent);
    {
        QPainter painter(&logo);
        icon(painter, Icon::Logo, QRectF(1, 1, 62, 62));
    }
    setWindowIcon(QIcon(logo));

    createHomePage();
    createNavigation();
    createCameraPage();
    createOpenCvPage();
    createAlbumPage();
    createUvcPage();
    resize(DefaultWidth, DefaultHeight);
    setPage(Page::Home);
}

Ui::CanvasButton *Dashboard::button(const QString &name, const QString &label, const QRectF &rect,
                                    unsigned pages, Ui::CanvasButton::Paint paint)
{
    auto *b = new Ui::CanvasButton(rect.size(), std::move(paint), this);
    b->setObjectName(name);
    b->setText(label);
    b->setAccessibleName(label);
    place(b, rect, pages);
    return b;
}

void Dashboard::place(QWidget *widget, const QRectF &rect, unsigned pages)
{
    placements_.append({widget, rect, pages});
}

void Dashboard::setPage(Page page)
{
    if (cameraRecording_ && page != Page::Camera)
        return;
    page_ = page;
    cameraTab_->setChecked(page == Page::Camera);
    albumTab_->setChecked(page == Page::Album);
    updatePlacementVisibility();
    arrange();
    setFocus(Qt::OtherFocusReason);
    update();
}

void Dashboard::updatePlacementVisibility()
{
    const unsigned pageMask = 1u << static_cast<unsigned>(page_);
    for (const auto &item : placements_) {
        bool visible = (item.pages & pageMask) != 0;
        if (item.widget == recordStopButton_)
            visible = page_ == Page::Camera && cameraRecording_;
        else if (page_ == Page::Camera && cameraRecording_)
            visible = false;
        item.widget->setVisible(visible);
    }
}

void Dashboard::setStatusMessage(const QString &message)
{
    statusMessage_ = message;
    update();
}

void Dashboard::setHomeModeSummary(const QString &summary)
{
    homeModeSummary_ = summary;
    update();
}

void Dashboard::setModeEntriesEnabled(bool cameraEnabled, bool uvcEnabled,
                                      bool openCvEnabled)
{
    cameraModeButton_->setEnabled(cameraEnabled);
    uvcModeButton_->setEnabled(uvcEnabled);
    openCvModeButton_->setEnabled(openCvEnabled);
}

void Dashboard::setNavigationEnabled(bool enabled)
{
    cameraTab_->setEnabled(enabled);
    albumTab_->setEnabled(enabled);
}

void Dashboard::setCameraUiState(bool snapshotEnabled, bool snapshotPending, bool recordEnabled,
                                 bool recording, bool recordPending, bool rtspAvailable,
                                 bool rtspActive, bool rtspPending, const QString &rtspStatus,
                                 const QString &recordElapsed)
{
    photoButton_->setEnabled(snapshotEnabled);
    photoButton_->setAccessibleName(snapshotPending ? QStringLiteral("拍照处理中") : QStringLiteral("拍照"));
    recordButton_->setEnabled(recordEnabled);
    recordButton_->setChecked(recording);
    recordButton_->setAccessibleName(recordPending ? QStringLiteral("录像处理中")
                                                   : recording ? QStringLiteral("停止录像")
                                                               : QStringLiteral("开始录像"));
    recordStopButton_->setEnabled(recording && recordEnabled);
    recordStopButton_->setAccessibleName(recordPending ? QStringLiteral("正在停止录像")
                                                       : QStringLiteral("停止录像"));
    rtspToggle_->setEnabled(rtspAvailable && !rtspPending);
    rtspToggle_->setChecked(rtspActive);
    rtspToggle_->setAccessibleName(rtspPending ? QStringLiteral("RTSP 处理中")
                                              : rtspActive ? QStringLiteral("停止 RTSP")
                                                           : QStringLiteral("启动 RTSP"));
    cameraRtspStatus_ = rtspStatus;
    cameraRecordElapsed_ = recordElapsed;
    if (recording && page_ != Page::Camera) {
        page_ = Page::Camera;
        cameraTab_->setChecked(true);
        albumTab_->setChecked(false);
    }
    cameraRecording_ = recording;
    updatePlacementVisibility();
    arrange();
    update();
}

void Dashboard::setUvcUiState(bool active, bool controlEnabled, const QString &status)
{
    uvcEnabled_ = active;
    uvcStatus_ = status;
    uvcToggleButton_->setEnabled(controlEnabled);
    uvcToggleButton_->setChecked(active);
    const QString label = active ? QStringLiteral("停止 UVC") : QStringLiteral("启用 UVC");
    uvcToggleButton_->setText(label);
    uvcToggleButton_->setAccessibleName(label);
    update();
}

void Dashboard::setAlbumItems(const QStringList &labels, int currentIndex)
{
    const QSignalBlocker blocker(mediaList_);
    mediaList_->clear();
    mediaList_->addItems(labels);
    if (currentIndex >= 0 && currentIndex < mediaList_->count())
        mediaList_->setCurrentRow(currentIndex);
}

void Dashboard::setAlbumPhoto(const QPixmap &photo, const QString &caption)
{
    albumPhoto_ = photo;
    albumVideoRect_ = QRectF();
    albumVideoVisible_ = false;
    albumCaption_ = caption;
    update();
}

void Dashboard::setAlbumVideo(const QRectF &designRect, const QString &caption)
{
    albumPhoto_ = QPixmap();
    albumVideoRect_ = designRect;
    albumVideoVisible_ = designRect.isValid();
    albumCaption_ = caption;
    update();
}

void Dashboard::setAlbumEmpty(const QString &message)
{
    albumPhoto_ = QPixmap();
    albumVideoRect_ = QRectF();
    albumVideoVisible_ = false;
    albumCaption_ = message;
    update();
}

void Dashboard::setAlbumPlaybackState(bool interactionEnabled, bool navigationEnabled,
                                      bool playEnabled, bool playing,
                                      const QString &progress)
{
    mediaList_->setEnabled(interactionEnabled);
    photosFilter_->setEnabled(interactionEnabled);
    videosFilter_->setEnabled(interactionEnabled);
    previousMediaButton_->setEnabled(navigationEnabled && mediaList_->currentRow() > 0);
    playMediaButton_->setEnabled(playEnabled);
    playMediaButton_->setChecked(playing);
    playMediaButton_->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
    nextMediaButton_->setEnabled(navigationEnabled && mediaList_->currentRow() >= 0 &&
                                 mediaList_->currentRow() + 1 < mediaList_->count());
    albumProgress_ = progress;
    update();
}

void Dashboard::arrange()
{
    const qreal scale = qMin(width() / DesignWidth, height() / DesignHeight);
    const QSizeF size(DesignWidth * scale, DesignHeight * scale);
    canvas_ = QRectF(QPointF((width() - size.width()) / 2., (height() - size.height()) / 2.), size);
    for (const auto &item : placements_) {
        const QRectF r(canvas_.left() + item.rect.x() * scale, canvas_.top() + item.rect.y() * scale,
                       item.rect.width() * scale, item.rect.height() * scale);
        item.widget->setGeometry(r.toRect());
    }
}

void Dashboard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    arrange();
}

void Dashboard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (page_ == Page::Camera) {
        // 清除整个 Camera 背景，保留独立 overlay 控件自身的透明度。
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        p.fillRect(rect(), Theme::Window::Canvas);
    }
    p.translate(canvas_.topLeft());
    p.scale(canvas_.width() / DesignWidth, canvas_.height() / DesignHeight);
    if (page_ != Page::Camera) {
        p.fillRect(QRectF(0, 0, DesignWidth, DesignHeight),
                   linearGradient({0, 0}, {1500, 960}, Theme::Window::Background));
    }
    switch (page_) {
    case Page::Home:
        paintHome(p);
        break;
    case Page::Camera:
        paintCamera(p);
        break;
    case Page::Album:
        paintAlbum(p);
        break;
    case Page::Uvc:
        paintUvc(p);
        break;
    case Page::OpenCv:
        paintOpenCv(p);
        break;
    }
}

void Dashboard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && windowHandle()) {
        Qt::Edges edges;
        if (event->pos().x() < 5)
            edges |= Qt::LeftEdge;
        if (event->pos().x() >= width() - 5)
            edges |= Qt::RightEdge;
        if (event->pos().y() < 5)
            edges |= Qt::TopEdge;
        if (event->pos().y() >= height() - 5)
            edges |= Qt::BottomEdge;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        if (edges && !isFullScreen() && !isMaximized())
            windowHandle()->startSystemResize(edges);
        else if (isInTitleBar(event->pos()))
            windowHandle()->startSystemMove();
#else
        Q_UNUSED(edges)
#endif
    }
    QWidget::mousePressEvent(event);
}

void Dashboard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isInTitleBar(event->pos()))
        isMaximized() ? showNormal() : showMaximized();
    QWidget::mouseDoubleClickEvent(event);
}

bool Dashboard::isInTitleBar(const QPoint &position) const
{
    const qreal titleHeight = page_ == Page::Home ? HeaderHeight : NavigationDragHeight;
    return position.y() < canvas_.top() + canvas_.height() * titleHeight / DesignHeight;
}

void Dashboard::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11) {
        isFullScreen() ? showNormal() : showFullScreen();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (isFullScreen() || isMaximized())
            showNormal();
        else if (page_ != Page::Home && !cameraRecording_) {
            setPage(Page::Home);
            emit homePageRequested();
        }
        else
            close();
        return;
    }
    QWidget::keyPressEvent(event);
}
