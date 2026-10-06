# twin::ptfeed — scripted Physical-Twin feeds for monitor-mode runtimes (untrusted demo/test source).
twin_add_library(ptfeed
    SOURCES feed.cpp
    PUBLIC_DEPS twin::json)
