PROGS=test test2 panel.vpi
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

all: $(PROGS)

.v.: panel.vh panel.vpi
	iverilog -o $@  -m ./panel.vpi $<

test: test.v panel.vh simple_clock.vh panel.vpi
	iverilog -o test -m ./panel.vpi test.v

test2: test2.v panel.vh simple_clock.vh panel.vpi
	iverilog -o test2 -m ./panel.vpi test2.v

# Let iverilog-vpi build the VPI plugin from the compiled UI component
# and VPI source.

panel.vpi: vpi.c ../sim.h ../libblink_static.a
	iverilog-vpi --name=panel $(GTK_INCS) -I.. -L.. -lblink_static  $(GTK_LIBS) vpi.c

clean:
	rm -f $(PROGS) *.o *~ core
