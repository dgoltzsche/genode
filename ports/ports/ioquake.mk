QUAKE3      = ioq3
QUAKE3_URL  = https://github.com/raspberrypi/quake3.git
PORTS += $(QUAKE3)

prepare:: $(CONTRIB_DIR)/$(QUAKE3)

$(CONTRIB_DIR)/$(QUAKE3):
	$(VERBOSE)git clone $(QUAKE3_URL) $@