include $(TOPDIR)/rules.mk

PKG_NAME:=wireless-rate-limiter
PKG_VERSION:=1
PKG_RELEASE:=1

PKG_BUILD_PARALLEL:=1

PKG_FILE_DEPENDS:=$(CURDIR)/../..

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -R $(CURDIR)/../../src/* $(PKG_BUILD_DIR)
endef

define Package/wireless-rate-limiter
  SECTION:=net
  CATEGORY:=Network
  DEPENDS:=+libubox +libubus +libblobmsg-json +tc +kmod-sched-core +kmod-ifb
  TITLE:=Wireless Rate Limiter
endef

define Package/wireless-rate-limiter/conffiles
/etc/config/wireless-rate-limiter
endef

define Package/wireless-rate-limiter/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/config $(1)/lib/wireless-rate-limiter

	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wireless-rate-limiter $(1)/usr/bin/wireless-rate-limiter

	$(INSTALL_BIN) ./files/wireless-rate-limiter.init $(1)/etc/init.d/wireless-rate-limiter

	$(CP) ./files/wireless-rate-limiter.uci $(1)/etc/config/wireless-rate-limiter

	$(CP) ./files/htb-shared.sh $(1)/lib/wireless-rate-limiter/htb-shared.sh
	$(INSTALL_BIN) ./files/htb-client.sh $(1)/lib/wireless-rate-limiter/htb-client.sh
	$(INSTALL_BIN) ./files/htb-netdev.sh $(1)/lib/wireless-rate-limiter/htb-netdev.sh
endef

$(eval $(call BuildPackage,wireless-rate-limiter))
