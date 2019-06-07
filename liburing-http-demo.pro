TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

#QMAKE_CXXFLAGS += -DUSE_LIBAIO=1

SOURCES += \
        global.cpp \
        main.cpp

LIBS += -luring -lboost_context -lfmt -laio

HEADERS += \
    coroutine.hpp \
    global.hpp \
    yield.hpp
