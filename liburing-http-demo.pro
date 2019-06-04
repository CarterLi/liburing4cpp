TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        global.cpp \
        main.cpp

LIBS += -luring -lboost_context -lfmt

HEADERS += \
    coroutine.hpp \
    global.hpp \
    yield.hpp
