#include "../dashboard.h"
#include "../ui/button_styles.h"

using namespace Ui;

void Dashboard::createNavigation()
{
    cameraTab_ =
        button(QStringLiteral("cameraTab"), QStringLiteral("Camera"), QRectF(24, 4, 220, 67),
               CameraPage | AlbumPage, ButtonStyles::navigationTab(Icon::Camera, QStringLiteral("Camera")));
    albumTab_ =
        button(QStringLiteral("albumTab"), QStringLiteral("相册"), QRectF(254, 4, 220, 67),
               CameraPage | AlbumPage, ButtonStyles::navigationTab(Icon::Photo, QStringLiteral("相册")));
    cameraTab_->setCheckable(true);
    albumTab_->setCheckable(true);
    connect(cameraTab_, &QAbstractButton::clicked, this, [this] {
        setPage(Page::Camera);
        emit cameraPageRequested();
    });
    connect(albumTab_, &QAbstractButton::clicked, this, [this] {
        setPage(Page::Album);
        emit albumPageRequested();
    });

    homeButton_ = button(QStringLiteral("homeButton"), QStringLiteral("返回主页"),
                         QRectF(1370, 12, 206, 54),
                         CameraPage | AlbumPage | UvcPage | OpenCvPage,
                         ButtonStyles::back());
    connect(homeButton_, &QAbstractButton::clicked, this, [this] {
        setPage(Page::Home);
        emit homePageRequested();
    });
}
