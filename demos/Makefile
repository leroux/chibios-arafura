SUBDIRS := ARMCM4-STM32F303-DISCOVERY_hs ARMCM4-STM32F407-DISCOVERY_hs

all clean:
	@for i in $(SUBDIRS); do \
		$(MAKE) -C $$i $@; \
	done

.PHONY: all clean $(SUBDIRS)
