ifneq ($(KBUILD_EXTMOD),)
# We only get here on kernels 2.6.0-2.6.9 .
# For newer kernels, Kbuild will be included directly by the kernel
# build system.
include $(src)/Kbuild

else

FIRM_DIR	:= ../../firmware

OCTASIC_OBJS:=$(shell ../oct612x/octasic-helper objects ../oct612x)
OCTASIC_CFLAGS:=$(shell ../oct612x/octasic-helper cflags ../oct612x) -Wno-undef

all: wct4xxp.o

%.o: %.c
	$(CC) $(KFLAGS) $(OCTASIC_CFLAGS) -o $@ -c $<

base.o: ../zaptel.h vpm450m.h wct4xxp.h

vpm450m.o: vpm450m.h ../oct612x/include/oct6100api/oct6100_api.h

$(FIRM_DIR)/zaptel-fw-oct6114-064.o: base.o
	$(MAKE) -C $(FIRM_DIR) zaptel-fw-oct6114-064.o

$(FIRM_DIR)/zaptel-fw-oct6114-128.o: base.o
	$(MAKE) -C $(FIRM_DIR) zaptel-fw-oct6114-128.o

wct4xxp.o: base.o vpm450m.o $(OCTASIC_OBJS) $(FIRM_DIR)/zaptel-fw-oct6114-064.o $(FIRM_DIR)/zaptel-fw-oct6114-128.o
	$(LD) -r -o $@ $^

clean:
	rm -f *.o
	rm -f $(OCTASIC_OBJS)

endif
