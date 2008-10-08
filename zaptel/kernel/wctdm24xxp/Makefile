ifneq ($(KBUILD_EXTMOD),)
# We only get here on kernels 2.6.0-2.6.9 .
# For newer kernels, Kbuild will be included directly by the kernel
# build system.
include $(src)/Kbuild

else

all: wctdm24xxp.o

%.o: %.c
	$(CC) $(KFLAGS) -o $@ -c $<

base.o: ../zaptel.h GpakCust.h ../wctdm.h

GpakCust.o: GpakCust.h

../firmware/zaptel-fw-vpmadt032.o: base.o
	$(MAKE) -C ../firmware zaptel-fw-vpmadt032.o

wctdm24xxp.o: base.o GpakCust.o GpakApi.o ../firmware/zaptel-fw-vpmadt032.o
	$(LD) -r -o $@ $^

clean:
	rm -f *.o

endif
