include $(TOPDIR)/rules.mk
PKG_NAME:=opencv
PKG_VERSION:=4.1.0
PKG_RELEASE:=5
PKG_SOURCE:=opencv-$(PKG_VERSION).zip
PKG_EXTRA_SOURCE:=opencv_contrib-$(PKG_VERSION).zip
ADE_SOURCE:=ade-0.1.1d.zip
PKG_SOURCE_PROTO:=linkfile
PKG_SOURCE_URL:=$(LICHEE_PLATFORM_DIR)/thirdparty/vision/opencv
PKG_MD5SUM:=5c5a9ce3519415b263d512e7f6a1e2af
PKG_HASH:=2c75b129da2e2c8728d168b7bf14ceca2da0ebe938557b109bae6742855ede13
PKG_BUILD_DIR:=$(BUILD_DIR)/opencv-$(PKG_VERSION)
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1
PKG_LICENSE:=BSD-3-Clause
PKG_CONFIG_DEPENDS:=CONFIG_ENABLE_SUNXIVIN_ISP
include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Download/opencv-contrib
  FILE:=opencv_contrib-4.1.0.zip
  URL:=$(LICHEE_PLATFORM_DIR)/thirdparty/vision/opencv
  PROTO:=linkfile
  MD5SUM:=3cd00bbfdebb69ad24756ccfb801ebac
  HASH:=b4013495ac6c4dd05dcad1c90b6c731b488a1d775835175327f3c20884269715
endef
$(eval $(call Download,opencv-contrib))

define Download/opencv-ade
  FILE:=ade-0.1.1d.zip
  URL:=$(LICHEE_PLATFORM_DIR)/thirdparty/vision/opencv
  PROTO:=linkfile
  MD5SUM:=37479d90e3a5d47f132f512b22cbe206
  HASH:=881255f64d77741b5707b41379a78aa45148a202ed26333b5b474db667db05e5
endef
$(eval $(call Download,opencv-ade))

CMAKE_SOURCE_DIR:=$(PKG_BUILD_DIR)/opencv_build
# TinyVision offline patches resolve ADE/downloads from the package build root.
CMAKE_BINARY_SUBDIR:=
CMAKE_OPTIONS += \
 -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_SKIP_RPATH=ON \
 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
 -DBUILD_LIST=core,dnn,imgcodecs,imgproc,ml,videoio \
 -DOPENCV_EXTRA_MODULES_PATH=$(PKG_BUILD_DIR)/opencv_contrib-4.1.0/modules \
 -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF \
 -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF \
 -DWITH_QT=OFF -DWITH_GTK=OFF -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF \
 -DWITH_OPENCL=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_TIFF=OFF \
 -DWITH_OPENEXR=OFF -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF \
 -DENABLE_PRECOMPILED_HEADERS=OFF -DCV_TRACE=OFF -DOPENCV_ENABLE_NONFREE=OFF \
 -DWITH_V4L=ON

ifeq ($(CONFIG_ENABLE_SUNXIVIN_ISP),y)
CMAKE_OPTIONS += -DENABLE_SUNXIVINISP=ON -DWITH_VINISP=ON -DSUNXI_ISP_API_ROOT=$(STAGING_DIR)/usr
else
CMAKE_OPTIONS += -DENABLE_SUNXIVINISP=OFF -DWITH_VINISP=OFF
endif
define Package/opencv
  SECTION:=libs
  CATEGORY:=nopiskl_apps
  SUBMENU:=add_patch
  TITLE:=OpenCV 4.1.0 with current-SDK VIN adaptation
  MENU:=1
  DEPENDS:=+libstdcpp +libpthread +librt +libatomic +libpng +libjpeg-turbo +zlib +ENABLE_SUNXIVIN_ISP:libAWIspApi
endef
define Package/opencv/config
  if PACKAGE_opencv
    config ENABLE_SUNXIVIN_ISP
      bool "Enable sunxi VIN / AW ISP adapter"
      default y
  endif
endef
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	unzip -q $(DL_DIR)/$(PKG_SOURCE) -d $(PKG_BUILD_DIR)
	mv $(PKG_BUILD_DIR)/opencv-$(PKG_VERSION) $(PKG_BUILD_DIR)/opencv_build
	unzip -q $(DL_DIR)/$(PKG_EXTRA_SOURCE) -d $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)/3rdparty/ade
	unzip -q $(DL_DIR)/$(ADE_SOURCE) -d $(PKG_BUILD_DIR)/3rdparty/ade
	$(Build/Patch)
endef
define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/include/. $(1)/usr/include/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/. $(1)/usr/lib/
endef
define Package/opencv/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libopencv_*.so* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,opencv))
