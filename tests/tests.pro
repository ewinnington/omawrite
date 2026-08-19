QT += core gui quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omaths

INCLUDEPATH += ../src
SOURCES += \
    tst_omaths.cpp \
    ../src/backend.cpp \
    ../src/mathrenderer.cpp \
    ../src/mathscanner.cpp \
    ../src/markdownhighlighter.cpp
HEADERS += \
    ../src/backend.h \
    ../src/mathrenderer.h \
    ../src/mathscanner.h \
    ../src/markdownhighlighter.h
RESOURCES += ../src/resources.qrc

QT += widgets printsupport quickcontrols2 quickdialogs2 dbus svg qml
