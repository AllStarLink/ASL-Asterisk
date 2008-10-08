ifneq ($(KBUILD_EXTMOD),)
# We only get here on kernels 2.6.0-2.6.9 .
# For newer kernels, Kbuild will be included directly by the kernel
# build system.
include $(src)/Kbuild

else

# building for 2.4 kernels means no VPM support, so none of the VPM support
# modules are included in the Makefile rules

all: wcte12xp.o

%.o: %.c
	$(CC) $(KFLAGS) -o $@ -c $<

base.o: ../zaptel.h

wcte12xp.o: base.o
	$(LD) -r -o $@ $^

clean:
	rm -f *.o

endif
