LOCAL_PATH:= $(call my-dir)

svc_c_flags =	\
	-Wall -Wextra -Werror \

ifneq ($(TARGET_USES_64_BIT_BINDER),true)
ifneq ($(TARGET_IS_64_BIT),true)
svc_c_flags += -DBINDER_IPC_32BIT=1
endif
endif

include $(CLEAR_VARS)
LOCAL_SRC_FILES := hw_service_manager.cpp
LOCAL_CFLAGS += $(svc_c_flags)
LOCAL_MODULE := hwservicemanager
LOCAL_C_INCLUDES := system/libhwbinder/include
LOCAL_INIT_RC := hwservicemanager.rc
LOCAL_SHARED_LIBRARIES := liblog libselinux libhidl libhwbinder libutils
include $(BUILD_EXECUTABLE)
