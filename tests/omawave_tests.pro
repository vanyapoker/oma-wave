QT += core gui quick quickcontrols2 testlib dbus network
CONFIG += c++17 testcase
TARGET = omawave_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/catalog.h ../src/controlserver.h ../src/lyrics.h ../src/paths.h ../src/player.h \
    ../src/settings.h ../src/theme.h ../src/translator.h ../src/visualizer.h ../src/wave.h
SOURCES += omawave_tests.cpp ../src/catalog.cpp ../src/controlserver.cpp ../src/lyrics.cpp ../src/paths.cpp \
    ../src/player.cpp ../src/settings.cpp ../src/theme.cpp ../src/translator.cpp ../src/visualizer.cpp ../src/wave.cpp
