PROGS=../libblink.so ../libblink_static.a

all: $(PROGS)

GTK_INCS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

GLIB_INCS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)


# Library. Static version has a different name for use with iverilog-vpi.

../libblink_static.a: sim.o panel.o pixbuf.o
	ar rs $@ $^

../libblink.so: sim.o panel.o pixbuf.o blink_fps.o
	ld -shared -Bsymbolic -o $@ $^ $(GTK_LIBS)

# Make stand-alone UI test program.

panel: panel_main.o pixbuf.o
	gcc -Wall -DMAIN -o panel panel_main.o pixbuf.o $(GTK_LIBS) 

panel_main.o: panel_main.c panel.h
	gcc -Wall -DMAIN -c  -o panel_main.o $(GTK_INCS) $<

panel.o: panel.c sim.h panel.h
	gcc -Wall -c -fPIC -DWINDOW_HEADING -o panel.o $(GTK_INCS) $<

pixbuf.o: pixbuf.c panel.h
	gcc -Wall -c -fPIC -o pixbuf.o $(GTK_INCS) $<

sim.o: sim.c sim.h panel.h
	gcc -Wall -c -fPIC -o sim.o $(GLIB_INCS) $<

clean:
	rm -f $(PROGS) *.o *~ core
