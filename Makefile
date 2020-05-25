PROJECT_NAME := picviewer
APP_DEBUG := 1
ifdef APP_DEBUG
CXXFLAGS += -DAPP_DEBUG
ifdef BT_DEBUG
CXXFLAGS += -DBT_DEBUG
endif
endif
CXXFLAGS+=-Ofast -DAPP_TEXTSIZE2=1

include $(IDF_PATH)/make/project.mk
