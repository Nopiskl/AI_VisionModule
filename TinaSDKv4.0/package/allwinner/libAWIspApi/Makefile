include $(TOPDIR)/rules.mk
PKG_NAME:=libAWIspApi
PKG_VERSION:=1.0.0
PKG_RELEASE:=2
PKG_BUILD_DIR:=$(COMPILE_DIR)/libAWIspApi-$(PKG_VERSION)
PKG_INSTALL:=1
PKG_BUILD_DEPENDS:=eyesee-mpp-middleware
include $(BUILD_DIR)/package.mk
include $(BUILD_DIR)/cmake.mk
AWISP_SOURCE_DIR:=$(TOPDIR)/external/libAWIspApi/isp600
AWISP_MPP_SOURCE_ROOT?=$(TOPDIR)/external/eyesee-mpp/middleware/sun8iw21
CMAKE_OPTIONS += -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_SKIP_RPATH=ON \
 -DSUNXI_MPP_SOURCE_ROOT=$(AWISP_MPP_SOURCE_ROOT) -DAWISP_LIBRARY_DIR=$(STAGING_DIR)/usr/lib/eyesee-mpp \
 -DAWISP_SYSTEM_LIBRARY_DIR=$(STAGING_DIR)/usr/lib
define Package/libAWIspApi
  SECTION:=libs
  CATEGORY:=nopiskl_apps
  TITLE:=AWIspApi wrapper for current sun8iw21 ISP600
  DEPENDS:=+libstdcpp +libpthread +glog +libunwind
endef
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(AWISP_SOURCE_DIR)/. $(PKG_BUILD_DIR)/
endef
define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr
	$(CP) $(PKG_INSTALL_DIR)/usr/. $(1)/usr/
endef
define Package/libAWIspApi/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libAWIspApi.so* $(1)/usr/lib/
endef
$(eval $(call BuildPackage,libAWIspApi))
