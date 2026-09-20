#pragma once

#include "ui/canvas_button.h"

#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QStringList>
#include <QWidget>

class QListWidget;

class Dashboard : public QWidget
{
    Q_OBJECT

public:
    enum class Page { Home, Camera, Album, Uvc, OpenCv };

    explicit Dashboard(QWidget *parent = nullptr);

    void setPage(Page page);
    Page currentPage() const { return page_; }
    bool isUvcEnabled() const { return uvcEnabled_; }

    void setStatusMessage(const QString &message);
    void setHomeModeSummary(const QString &summary);
    void setModeEntriesEnabled(bool cameraEnabled, bool uvcEnabled,
                               bool openCvEnabled);
    void setNavigationEnabled(bool enabled);
    void setCameraUiState(bool snapshotEnabled, bool snapshotPending, bool recordEnabled,
                          bool recording, bool recordPending, bool rtspAvailable,
                          bool rtspActive, bool rtspPending, const QString &rtspStatus,
                          const QString &recordElapsed);
    void setUvcUiState(bool active, bool controlEnabled, const QString &status);
    void setAlbumItems(const QStringList &labels, int currentIndex);
    void setAlbumPhoto(const QPixmap &photo, const QString &caption);
    void setAlbumVideo(const QRectF &designRect, const QString &caption);
    void setAlbumEmpty(const QString &message);
    void setAlbumPlaybackState(bool interactionEnabled, bool navigationEnabled,
                               bool playEnabled, bool playing,
                               const QString &progress);

signals:
    void cameraPageRequested();
    void albumPageRequested();
    void uvcPageRequested();
    void homePageRequested();
    void exitApplicationRequested();
    void openCvSessionRequested();
    void snapshotRequested();
    void recordToggleRequested();
    void rtspToggleRequested(bool enabled);
    void uvcToggleRequested(bool enabled);
    void albumFilterRequested(bool photos);
    void albumItemRequested(int row);
    void previousMediaRequested();
    void nextMediaRequested();
    void playPauseRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    static constexpr unsigned HomePage = 1u << static_cast<unsigned>(Page::Home);
    static constexpr unsigned CameraPage = 1u << static_cast<unsigned>(Page::Camera);
    static constexpr unsigned AlbumPage = 1u << static_cast<unsigned>(Page::Album);
    static constexpr unsigned UvcPage = 1u << static_cast<unsigned>(Page::Uvc);
    static constexpr unsigned OpenCvPage = 1u << static_cast<unsigned>(Page::OpenCv);

    struct Placement {
        QWidget *widget;
        QRectF rect;
        unsigned pages;
    };

    QList<Placement> placements_;
    QRectF canvas_;
    Page page_ = Page::Home;
    bool uvcEnabled_ = false;
    QString statusMessage_;
    QString homeModeSummary_{QStringLiteral("当前无活动后端；同一时间仅可启用一种工作模式")};
    QString cameraRtspStatus_{QStringLiteral("RTSP 未启用")};
    QString cameraRecordElapsed_{QStringLiteral("00:00")};
    QString uvcStatus_{QStringLiteral("点击下方按钮启用 UVC 输出")};
    QString albumCaption_{QStringLiteral("未选择图片/视频")};
    QString albumProgress_{QStringLiteral("00:00 / 00:00")};
    QPixmap albumPhoto_;
    QRectF albumVideoRect_;
    bool albumVideoVisible_ = false;
    bool cameraRecording_ = false;
    Ui::CanvasButton *cameraTab_ = nullptr;
    Ui::CanvasButton *albumTab_ = nullptr;
    Ui::CanvasButton *cameraModeButton_ = nullptr;
    Ui::CanvasButton *uvcModeButton_ = nullptr;
    Ui::CanvasButton *openCvModeButton_ = nullptr;
    Ui::CanvasButton *photoButton_ = nullptr;
    Ui::CanvasButton *recordButton_ = nullptr;
    Ui::CanvasButton *recordStopButton_ = nullptr;
    Ui::CanvasButton *rtspToggle_ = nullptr;
    Ui::CanvasButton *detectionToggle_ = nullptr;
    Ui::CanvasButton *uvcToggleButton_ = nullptr;
    Ui::CanvasButton *photosFilter_ = nullptr;
    Ui::CanvasButton *videosFilter_ = nullptr;
    Ui::CanvasButton *previousMediaButton_ = nullptr;
    Ui::CanvasButton *playMediaButton_ = nullptr;
    Ui::CanvasButton *nextMediaButton_ = nullptr;
    Ui::CanvasButton *homeButton_ = nullptr;
    QListWidget *mediaList_ = nullptr;

    void arrange();
    void updatePlacementVisibility();
    bool isInTitleBar(const QPoint &position) const;
    void place(QWidget *widget, const QRectF &rect, unsigned pages);
    Ui::CanvasButton *button(const QString &name, const QString &label, const QRectF &rect,
                             unsigned pages, Ui::CanvasButton::Paint paint);

    void createNavigation();
    void createHomePage();
    void createCameraPage();
    void createAlbumPage();
    void createUvcPage();
    void createOpenCvPage();
    void paintHome(QPainter &p);
    void paintCamera(QPainter &p);
    void paintAlbum(QPainter &p);
    void paintUvc(QPainter &p);
    void paintOpenCv(QPainter &p);
};
