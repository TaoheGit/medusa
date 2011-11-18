
PROJ_DIR ?= .

LIBCHUNFENG_PATH ?= $(PROJ_DIR)/../libchunfeng
LDFLAGS_LIBCHUNFENG = -lchunfeng -lm -lcrypt
LIBCHUNFENG_LIB_PATH = $(LIBCHUNFENG_PATH)
LDFLAGS_LIBCHUNFENG += -L$(LIBCHUNFENG_LIB_PATH)
CFLAGS_LIBCHUNFENG += -I$(LIBCHUNFENG_PATH)/include
