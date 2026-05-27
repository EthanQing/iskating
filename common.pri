# ---- root dirs ----
ROOT_DIR = $$TOP_PWD
BIN_DIR  = $$PWD/../bin

# ---- separate output for app/lib ----
equals(TEMPLATE, app) {
    # 可执行程序输出到 bin
    DESTDIR = $$BIN_DIR
}

equals(TEMPLATE, lib) { 
    # Windows：共享库（dll）建议放release
    win32 {
        DLLDESTDIR = $$BIN_DIR 
        QMAKE_CXXFLAGS += /Od
        QMAKE_CFLAGS   += /Od
    }
}

# ---- intermediate files go to build dir to avoid conflict ----
#MOC_DIR     = $$OUT_PWD/.moc/$$BUILD_TYPE
#UI_DIR      = $$OUT_PWD/.ui/$$BUILD_TYPE
#RCC_DIR     = $$OUT_PWD/.rcc/$$BUILD_TYPE
#OBJECTS_DIR = $$OUT_PWD/.obj/$$BUILD_TYPE

message("TEMPLATE=$$TEMPLATE TARGET=$$TARGET DESTDIR=$$DESTDIR DLLDESTDIR=$$DLLDESTDIR")
