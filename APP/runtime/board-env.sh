# Current physical 480x800, Qt logical 800x480. Adjust for another board.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-linuxfb:fb=/dev/fb0:rotation=90}"
export QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-/usr/lib/qt5/plugins}"
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="${QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS:-/dev/input/event3:rotate=270:single-touch:range=480x800}"
export QT_QPA_FB_HIDECURSOR="${QT_QPA_FB_HIDECURSOR:-1}"
