SRC_S += crt0.s

SRC_CC += _main.cc init_main_thread.cc

REP_INC_DIR += src/platform

vpath %.cc $(BASE_DIR)/src/platform
