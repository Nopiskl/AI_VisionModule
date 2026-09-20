#include "../dashboard.h"
#include "../ui/button_styles.h"
#include "../ui/painting.h"

#include <QButtonGroup>
#include <QListWidget>

using namespace Ui;

namespace
{
const QRectF PreviewRect(288, 80, 1288, 776);
}

void Dashboard::createAlbumPage()
{
    auto *mediaGroup = new QButtonGroup(this);
    const QStringList filters = {QStringLiteral("照片"), QStringLiteral("录像")};
    for (int i = 0; i < filters.size(); ++i) {
        auto *filter = button(i == 0 ? QStringLiteral("photosFilter") : QStringLiteral("videosFilter"),
                              filters[i], QRectF(34 + i * 112, 152, 112, 62), AlbumPage,
                              ButtonStyles::mediaFilter(i == 0 ? Icon::Photo : Icon::Play, filters[i]));
        filter->setCheckable(true);
        filter->setChecked(i == 0);
        mediaGroup->addButton(filter);
        if (i == 0)
            photosFilter_ = filter;
        else
            videosFilter_ = filter;
    }
    connect(photosFilter_, &QAbstractButton::clicked, this,
            [this] { emit albumFilterRequested(true); });
    connect(videosFilter_, &QAbstractButton::clicked, this,
            [this] { emit albumFilterRequested(false); });

    mediaList_ = new QListWidget(this);
    mediaList_->setObjectName(QStringLiteral("mediaList"));
    mediaList_->setAccessibleName(QStringLiteral("媒体文件列表"));
    mediaList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mediaList_->setTextElideMode(Qt::ElideMiddle);
    mediaList_->setStyleSheet(QStringLiteral(
        "QListWidget { background: rgba(0,0,0,38); border: 0; color: #d8e5f5;"
        " font-size: 11px; outline: 0; padding: 2px; }"
        "QListWidget::item { min-height: 24px; padding: 2px 3px; }"
        "QListWidget::item:selected { background: rgba(37,145,242,155);"
        " border-radius: 4px; color: white; }"));
    place(mediaList_, QRectF(32, 230, 228, 698), AlbumPage);
    connect(mediaList_, &QListWidget::currentRowChanged, this,
            &Dashboard::albumItemRequested);

    previousMediaButton_ =
        button(QStringLiteral("previousMediaButton"), QStringLiteral("上一项"),
               QRectF(806, 874, 64, 64), AlbumPage,
               ButtonStyles::playback(Icon::Previous, 64, false));
    playMediaButton_ = button(QStringLiteral("playMediaButton"), QStringLiteral("播放"),
                              QRectF(898, 868, 76, 76), AlbumPage,
                              ButtonStyles::playback(Icon::Play, 76, true));
    playMediaButton_->setCheckable(true);
    nextMediaButton_ = button(QStringLiteral("nextMediaButton"), QStringLiteral("下一项"),
                              QRectF(1002, 874, 64, 64), AlbumPage,
                              ButtonStyles::playback(Icon::Next, 64, false));
    for (auto *control : {previousMediaButton_, playMediaButton_, nextMediaButton_})
        control->setEnabled(false);
    connect(previousMediaButton_, &QAbstractButton::clicked, this,
            &Dashboard::previousMediaRequested);
    connect(nextMediaButton_, &QAbstractButton::clicked, this,
            &Dashboard::nextMediaRequested);
    connect(playMediaButton_, &QAbstractButton::clicked, this, [this](bool checked) {
        playMediaButton_->setChecked(!checked);
        emit playPauseRequested();
    });
}

void Dashboard::paintAlbum(QPainter &p)
{
    p.setPen(QPen(Theme::Navigation::Separator, 1.5));
    p.drawLine(QPointF(24, 71), QPointF(1576, 71));
    panel(p, QRectF(20, 78, 252, 866), Theme::Album::Sidebar);
    icon(p, Icon::Photo, QRectF(40, 102, 30, 30), Theme::Album::LibraryIcon);
    text(p, QRectF(84, 90, 164, 52), QStringLiteral("媒体库"), 28, Theme::TextPrimary, true);
    panel(p, QRectF(32, 152, 228, 62), Theme::Album::Filters);
    panel(p, PreviewRect, Theme::Album::Preview);
    if (albumVideoVisible_) {
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(albumVideoRect_, Qt::transparent);
        p.restore();
    } else if (!albumPhoto_.isNull()) {
        const QSizeF fitted = albumPhoto_.size().scaled(PreviewRect.size().toSize(),
                                                       Qt::KeepAspectRatio);
        const QRectF destination(PreviewRect.center().x() - fitted.width() / 2.,
                                 PreviewRect.center().y() - fitted.height() / 2.,
                                 fitted.width(), fitted.height());
        p.drawPixmap(destination, albumPhoto_, QRectF(albumPhoto_.rect()));
    } else {
        text(p, PreviewRect, albumCaption_, 30, Theme::Album::EmptyText, false,
             Qt::AlignCenter);
    }
    p.setPen(QPen(Theme::Window::PreviewBorder, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(PreviewRect, 18, 18);
    text(p, QRectF(1080, 878, 465, 54), albumProgress_, 22,
         Theme::Album::EmptyText, false, Qt::AlignRight | Qt::AlignVCenter);
    if (!statusMessage_.isEmpty())
        text(p, QRectF(500, 6, 850, 60), statusMessage_, 22, Theme::TextPrimary,
             false, Qt::AlignRight | Qt::AlignVCenter);
}
