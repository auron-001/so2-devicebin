LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := device

LOCAL_SRC_FILES := $(patsubst $(LOCAL_PATH)/%,%, \
    $(wildcard $(LOCAL_PATH)/src/*.cpp) \
    $(wildcard $(LOCAL_PATH)/src/*/*.cpp) \
    $(wildcard $(LOCAL_PATH)/src/*/*/*.cpp))

LOCAL_C_INCLUDES := $(LOCAL_PATH)/src

LOCAL_CFLAGS += -O2 -w -fvisibility=hidden -ffunction-sections -fdata-sections -fdiagnostics-color=always
LOCAL_LDFLAGS += -Wl,--gc-sections

include $(BUILD_EXECUTABLE)
