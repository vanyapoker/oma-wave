QT += core gui qml quick quickcontrols2 dbus network

CONFIG += c++17 release
TARGET = omawave
TEMPLATE = app

HEADERS += \
    src/catalog.h \
    src/controlserver.h \
    src/lyrics.h \
    src/mpris.h \
    src/paths.h \
    src/player.h \
    src/settings.h \
    src/theme.h \
    src/translator.h \
    src/visualizer.h

SOURCES += \
    src/catalog.cpp \
    src/controlserver.cpp \
    src/lyrics.cpp \
    src/main.cpp \
    src/mpris.cpp \
    src/paths.cpp \
    src/player.cpp \
    src/settings.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/visualizer.cpp

RESOURCES += src/resources.qrc
