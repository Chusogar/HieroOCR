# HieroOCR: el mismo código C que en escritorio (../../../../src), con
# android.c como punto de entrada.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

HIEROOCR_SRC := ../../../../src

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../SDL/include $(LOCAL_PATH)/../SDL_image/include \
                    $(LOCAL_PATH)/../SDL_ttf $(LOCAL_PATH)/$(HIEROOCR_SRC)

LOCAL_SRC_FILES := $(addprefix $(HIEROOCR_SRC)/, android.c app.c image.c segment.c classify.c \
                   mdc.c train.c dataset.c gui.c main.c)

LOCAL_CFLAGS := -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Wno-unused-parameter

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_image SDL2_ttf

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lm

include $(BUILD_SHARED_LIBRARY)
