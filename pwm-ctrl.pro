TEMPLATE =      app

CONFIG +=       debug_and_release \
                warn_on \
                copy_dir_files

debug:CONFIG += console

CONFIG -=       warn_off

QT +=           network

contains($$[QT_VERSION_MAJOR],5) {
    QT += widgets
}

TARGET =        copter-pwm-ctrl-qt

SOURCES +=      \
    CopterMotor.cpp \
    CopterCtrl.cpp \
    CopterAxis.cpp \
    MainWindow.cpp \
    Main.cpp \
    accelerometer.cpp

HEADERS +=      \
    CopterMotor.hpp \
    CopterCtrl.hpp \
    CopterAxis.hpp \
    MainWindow.hpp \
    accelerometer.hpp

FORMS += \       
    MainWindow.ui

QMAKE_CXXFLAGS += -std=c++11
QMAKE_CXXFLAGS_WARN_ON = -Wno-reorder

unix {
  target.path = $$[INSTALL_ROOT]/bin
  INSTALLS +=   target
}
