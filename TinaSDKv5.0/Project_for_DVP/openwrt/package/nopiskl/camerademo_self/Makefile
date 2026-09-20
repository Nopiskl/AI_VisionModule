include $(TOPDIR)/rules.mk

PKG_NAME:=camerademo_self
PKG_VERSION:=1.0
PKG_RELEASE:=2
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk


define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=nopiskl_apps
  SUBMENU:=add_patch
  TITLE:=Camera Demo Application
  DEPENDS:=+opencv +libstdcpp
endef

define Build/Compile
	$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		CXX="$(TARGET_CXX)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CXXFLAGS="$(filter-out -std=%,$(TARGET_CXXFLAGS)) -std=c++11" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		STAGING_DIR="$(STAGING_DIR)" all
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(PKG_NAME) 	$(1)/usr/bin/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
