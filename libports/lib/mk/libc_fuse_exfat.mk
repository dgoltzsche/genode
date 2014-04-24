include $(REP_DIR)/ports/exfat.inc
EXFAT_DIR = $(REP_DIR)/contrib/$(EXFAT)

SRC_C  = $(notdir $(EXFAT_DIR)/fuse/main.c)
SRC_CC = init.cc

LIBS   = libc libc_vfs libfuse libexfat

vpath %.c $(EXFAT_DIR)/fuse
vpath %.cc $(REP_DIR)/src/lib/exfat

SHARED_LIB = yes
