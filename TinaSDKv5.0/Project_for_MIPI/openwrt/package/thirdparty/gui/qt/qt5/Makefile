# Native Tina SDK source package.
include $(TOPDIR)/rules.mk
PKG_NAME:=qt5
PKG_VERSION:=5.12.9
PKG_RELEASE:=4
PKG_SOURCE:=qt-$(PKG_VERSION).tar.xz
PKG_SOURCE_PROTO:=linkfile
PKG_SOURCE_URL:=$(LICHEE_PLATFORM_DIR)/thirdparty/gui/qt
PKG_MD5SUM:=b81cd279d3bd2aa5e7bce8a9312d5ada
PKG_HASH:=e22483838efffccc8fab81b33960ca70238d3d6b2763510d7cd99c53255fba98
PKG_BUILD_DIR:=$(BUILD_DIR)/qt-$(PKG_VERSION)
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1
# qtbase always builds Gui/Widgets, even when only qt5-core is selected.
PKG_BUILD_DEPENDS:=libpng libjpeg-turbo freetype zlib
PKG_LICENSE:=LGPL-2.1-or-later GPL-3.0-or-later
include $(INCLUDE_DIR)/package.mk

# Standard Build/Prepare: SDK unpack + patches/series.
QT_SOURCE_DIR:=$(PKG_BUILD_DIR)
QT_BINARY_DIR:=$(PKG_BUILD_DIR)/build-qtbase
QT5_PKG_CONFIG:=$(if $(wildcard $(STAGING_DIR_HOST)/bin/pkg-config.real),$(STAGING_DIR_HOST)/bin/pkg-config.real,$(STAGING_DIR_HOST)/bin/pkg-config)
QT5_CXXFLAGS:=$(filter-out -fno-rtti -std=%,$(TARGET_CXXFLAGS))
QT5_ENV = TARGET_CC="$(TARGET_CC)" TARGET_CXX="$(TARGET_CXX)" \
 TARGET_AR="$(TARGET_AR) cqs" TARGET_RANLIB="$(TARGET_RANLIB)" \
 TARGET_OBJCOPY="$(TARGET_CROSS)objcopy" TARGET_CFLAGS="$(TARGET_CFLAGS)" \
 TARGET_CXXFLAGS="$(QT5_CXXFLAGS)" TARGET_LDFLAGS="$(TARGET_LDFLAGS)" \
 TARGET_INCDIRS="$(STAGING_DIR)/usr/include $(STAGING_DIR)/usr/include/freetype2" \
 TARGET_LIBDIRS="$(STAGING_DIR)/usr/lib"
define Package/qt5/Default
  SECTION:=libs
  CATEGORY:=nopiskl_apps
  SUBMENU:=add_patch
  URL:=https://www.qt.io/
endef

define Package/qt5-core
  $(call Package/qt5/Default)
  TITLE:=Qt 5.12.9 Core
  DEPENDS:=+libstdcpp +libpthread +librt +zlib
endef

define Package/qt5-network
  $(call Package/qt5/Default)
  TITLE:=Qt 5.12.9 Network
  DEPENDS:=+qt5-core
endef

define Package/qt5-gui
  $(call Package/qt5/Default)
  TITLE:=Qt 5.12.9 Gui
  DEPENDS:=+qt5-core +libfreetype +libpng +libjpeg-turbo
endef

define Package/qt5-widgets
  $(call Package/qt5/Default)
  TITLE:=Qt 5.12.9 Widgets
  DEPENDS:=+qt5-core +qt5-gui
endef

define Package/qt5-drivers-linuxfb
  $(call Package/qt5/Default)
  TITLE:=qt5-drivers-linuxfb
  DEPENDS:=+qt5-gui
endef

define Package/qt5-drivers-touchscreen
  $(call Package/qt5/Default)
  TITLE:=qt5-drivers-touchscreen
  DEPENDS:=+qt5-gui
endef

define Package/qt5-drivers-mouse
  $(call Package/qt5/Default)
  TITLE:=qt5-drivers-mouse
  DEPENDS:=+qt5-gui
endef

define Package/qt5-drivers-keyboard
  $(call Package/qt5/Default)
  TITLE:=qt5-drivers-keyboard
  DEPENDS:=+qt5-gui
endef

define Package/qt5-imageformats
  $(call Package/qt5/Default)
  TITLE:=qt5-imageformats
  DEPENDS:=+qt5-gui
endef

define Package/qt5-runtime
  $(call Package/qt5/Default)
  TITLE:=Qt runtime selection for nopiskl camera APP
  DEPENDS:=+qt5-core +qt5-network +qt5-gui +qt5-widgets +qt5-drivers-linuxfb +qt5-drivers-touchscreen +qt5-imageformats
endef

define Build/Configure
	mkdir -p $(QT_BINARY_DIR) $(QT_SOURCE_DIR)/qtbase/mkspecs/linux-ai-v85x-g++
	$(CP) ./files/qmake.conf ./files/qplatformdefs.h $(QT_SOURCE_DIR)/qtbase/mkspecs/linux-ai-v85x-g++/
	(cd $(QT_BINARY_DIR); \
		TARGET_CC="$(TARGET_CC)" TARGET_CXX="$(TARGET_CXX)" \
		TARGET_AR="$(TARGET_AR) cqs" TARGET_RANLIB="$(TARGET_RANLIB)" \
		TARGET_OBJCOPY="$(TARGET_CROSS)objcopy" \
		TARGET_CFLAGS="$(TARGET_CFLAGS)" TARGET_CXXFLAGS="$(QT5_CXXFLAGS)" \
		TARGET_LDFLAGS="$(TARGET_LDFLAGS) -Wl,-rpath-link,$(STAGING_DIR)/usr/lib" \
		TARGET_INCDIRS="$(STAGING_DIR)/usr/include $(STAGING_DIR)/usr/include/freetype2" \
		TARGET_LIBDIRS="$(STAGING_DIR)/usr/lib" \
		PKG_CONFIG="$(QT5_PKG_CONFIG)" \
		PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
		PKG_CONFIG_LIBDIR="$(STAGING_DIR)/usr/lib/pkgconfig" \
		PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig" \
		$(QT_SOURCE_DIR)/qtbase/configure \
		-prefix /usr -extprefix $(PKG_INSTALL_DIR)/usr -hostprefix $(PKG_INSTALL_DIR)/host \
		-plugindir /usr/lib/qt5/plugins -sysroot $(TOOLCHAIN_DIR) \
		-recheck-all -pkg-config -I $(STAGING_DIR)/usr/include -I $(STAGING_DIR)/usr/include/freetype2 -L $(STAGING_DIR)/usr/lib \
		-release -opensource -confirm-license -shared -xplatform linux-ai-v85x-g++ \
		-c++std c++14 -qpa linuxfb -linuxfb -evdev -gui -widgets \
		-system-zlib -system-libpng -system-libjpeg -system-freetype -qt-harfbuzz -qt-pcre \
		-no-opengl -no-eglfs -no-kms -no-gbm -no-directfb -no-xcb \
		-no-glib -no-dbus -no-libudev -no-libinput -no-mtdev -no-tslib -no-xkbcommon \
		-no-fontconfig -no-openssl -no-cups -no-gtk -no-icu -no-journald -no-syslog \
		-no-pch -no-ltcg -no-use-gold-linker -nomake tests -nomake examples)
endef

define Build/Compile
	$(QT5_ENV) $(MAKE) $(PKG_JOBS) -C $(QT_BINARY_DIR)
endef
define Build/Install
	$(QT5_ENV) $(MAKE) -C $(QT_BINARY_DIR) install
endef
define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr $(2)/bin $(1)/usr/lib/qt5/host/bin
	$(CP) $(PKG_INSTALL_DIR)/usr/. $(1)/usr/
	$(CP) $(PKG_INSTALL_DIR)/host/bin/. $(2)/bin/
	$(CP) $(PKG_INSTALL_DIR)/host/bin/. $(1)/usr/lib/qt5/host/bin/
	$(CP) $(PKG_INSTALL_DIR)/host/mkspecs $(1)/usr/lib/qt5/host/
	python3 ./files/fix-cmake-host.py "$(1)/usr/lib/cmake" "$(PKG_INSTALL_DIR)/host" "$(1)/usr/lib/qt5/host"
endef

define Package/qt5-core/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libQt5Core.so* $(1)/usr/lib/
endef

define Package/qt5-network/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libQt5Network.so* $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/lib/qt5/plugins/bearer
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/qt5/plugins/bearer/*.so $(1)/usr/lib/qt5/plugins/bearer/
endef

define Package/qt5-gui/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libQt5Gui.so* $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/etc/profile.d
	$(INSTALL_DATA) ./files/qt-env.sh $(1)/etc/profile.d/qt-env.sh
endef

define Package/qt5-widgets/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libQt5Widgets.so* $(1)/usr/lib/
endef

define Package/qt5-drivers-linuxfb/install
	$(INSTALL_DIR) $(1)/usr/lib/qt5/plugins/platforms
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/qt5/plugins/platforms/libqlinuxfb.so $(1)/usr/lib/qt5/plugins/platforms/
endef

define Package/qt5-drivers-touchscreen/install
	$(INSTALL_DIR) $(1)/usr/lib/qt5/plugins/generic
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/qt5/plugins/generic/libqevdevtouchplugin.so $(1)/usr/lib/qt5/plugins/generic/
endef

define Package/qt5-drivers-mouse/install
	$(INSTALL_DIR) $(1)/usr/lib/qt5/plugins/generic
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/qt5/plugins/generic/libqevdevmouseplugin.so $(1)/usr/lib/qt5/plugins/generic/
endef

define Package/qt5-drivers-keyboard/install
	$(INSTALL_DIR) $(1)/usr/lib/qt5/plugins/generic
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/qt5/plugins/generic/libqevdevkeyboardplugin.so $(1)/usr/lib/qt5/plugins/generic/
endef

define Package/qt5-imageformats/install
	$(INSTALL_DIR) $(1)/usr/lib/qt5/plugins/imageformats
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/qt5/plugins/imageformats/*.so $(1)/usr/lib/qt5/plugins/imageformats/
endef

define Package/qt5-runtime/install
	true
endef
$(eval $(call BuildPackage,qt5-core))
$(eval $(call BuildPackage,qt5-network))
$(eval $(call BuildPackage,qt5-gui))
$(eval $(call BuildPackage,qt5-widgets))
$(eval $(call BuildPackage,qt5-drivers-linuxfb))
$(eval $(call BuildPackage,qt5-drivers-touchscreen))
$(eval $(call BuildPackage,qt5-drivers-mouse))
$(eval $(call BuildPackage,qt5-drivers-keyboard))
$(eval $(call BuildPackage,qt5-imageformats))
$(eval $(call BuildPackage,qt5-runtime))
