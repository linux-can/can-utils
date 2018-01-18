LOCAL_PATH := $(call my-dir)

PRIVATE_LOCAL_CFLAGS := -O2 -g -W -Wall		\
			-DSO_RXQ_OVFL=40	\
			-DPF_CAN=29		\
			-DAF_CAN=PF_CAN \
			-Wno-error=unused-parameter \
			-Wno-error=sign-compare \
			-Wno-error=pointer-arith \
			-Wno-error=address-of-packed-member

#
# canlib
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := lib.c canframelen.c
LOCAL_MODULE := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_STATIC_LIBRARY)

#
# candump
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := candump.c
LOCAL_MODULE := candump
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# cansend
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := cansend.c
LOCAL_MODULE := cansend
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# bcmserver
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := bcmserver.c
LOCAL_MODULE := bcmserver
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)


#
# can-calc-bit-timing
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := can-calc-bit-timing.c
LOCAL_MODULE := can-calc-bit-timing
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# canbusload
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := canbusload.c
LOCAL_MODULE := canbusload
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# canfdtest
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := canfdtest.c
LOCAL_MODULE := canfdtest
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# cangen
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := cangen.c
LOCAL_MODULE := cangen
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# cangw
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := cangw.c
LOCAL_MODULE := cangw
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# canlogserver
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := canlogserver.c
LOCAL_MODULE := canlogserver
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# canplayer
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := canplayer.c
LOCAL_MODULE := canplayer
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# cansniffer
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := cansniffer.c
LOCAL_MODULE := cansniffer
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotpdump
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotpdump.c
LOCAL_MODULE := isotpdump
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotprecv
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotprecv.c
LOCAL_MODULE := isotprecv
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotpsend
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotpsend.c
LOCAL_MODULE := isotpsend
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotpserver
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotpserver.c
LOCAL_MODULE := isotpserver
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotpsniffer
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotpsniffer.c
LOCAL_MODULE := isotpsniffer.c
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotptun
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotptun.c
LOCAL_MODULE := isotptun
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# isotpperf
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := isotpperf.c
LOCAL_MODULE := isotpperf
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# log2asc
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := log2asc.c
LOCAL_MODULE := log2asc
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# log2long
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := log2long.c
LOCAL_MODULE := log2long
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libcan
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# slcan_attach
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := slcan_attach.c
LOCAL_MODULE := slcan_attach
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# slcand
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := slcand.c
LOCAL_MODULE := slcand
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)

#
# slcanpty
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := slcanpty.c
LOCAL_MODULE := slcanpty
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)
