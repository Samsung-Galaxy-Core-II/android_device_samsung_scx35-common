# Audio
PRODUCT_PACKAGES += \
	android.hardware.audio@2.0-impl \
	android.hardware.audio@2.0-service \
	android.hardware.audio.effect@2.0-impl

# Bluetooth
PRODUCT_PACKAGES += \
	android.hardware.bluetooth@1.0-service \
	android.hardware.bluetooth@1.0-impl

# Boardcast service
PRODUCT_PACKAGES += \
	android.hardware.broadcastradio@1.0-impl

# Camera
PRODUCT_PACKAGES += \
	android.hardware.camera.provider@2.4-service \
	android.hardware.camera.provider@2.4-impl-legacy \
 	camera.device@1.0-impl-legacy

# Configstore
PRODUCT_PACKAGES += \
	android.hardware.configstore@1.0-service

# DRM
PRODUCT_PACKAGES += \
	android.hardware.drm@1.0-service \
	android.hardware.drm@1.0-impl

# GPS
PRODUCT_PACKAGES += \
	android.hardware.gnss@1.0-impl

# Graphics
PRODUCT_PACKAGES += \
	android.hardware.graphics.allocator@2.0-impl \
	android.hardware.graphics.composer@2.1-impl \
	android.hardware.graphics.mapper@2.0-impl

# Keymaster
PRODUCT_PACKAGES += \
	android.hardware.keymaster@3.0-service \
	android.hardware.keymaster@3.0-impl

# Lights
PRODUCT_PACKAGES += \
	android.hardware.light@2.0-service \
	android.hardware.light@2.0-impl

# Media
PRODUCT_PACKAGES += \
	android.hardware.cas@1.0-service

# Memtrack
PRODUCT_PACKAGES += \
	android.hardware.memtrack@1.0-service \
	android.hardware.memtrack@1.0-impl

# PowerHAL
ifneq ($(SCX35_COMMON_POWERHAL_OVERRIDE),true)
PRODUCT_PACKAGES += \
	android.hardware.power@1.0-service.sc8830
endif

# RenderScript HAL
PRODUCT_PACKAGES += \
	android.hardware.renderscript@1.0-impl

# Sensors
PRODUCT_PACKAGES += \
	android.hardware.sensors@1.0-service \
	android.hardware.sensors@1.0-impl

# USB
PRODUCT_PACKAGES += \
	android.hardware.usb@1.0-service

# Vibrator
PRODUCT_PACKAGES += \
	android.hardware.vibrator@1.0-service \
	android.hardware.vibrator@1.0-impl

# WiFi
PRODUCT_PACKAGES += \
	android.hardware.wifi@1.0-service \
	android.system.net.netd@1.0

# Netutils
PRODUCT_PACKAGES += \
    	netutils-wrapper-1.0 \
    	android.system.net.netd@1.0 \
    	libandroid_net
