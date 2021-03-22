#include <stdlib.h>
#include <gtk/gtk.h>

#include "sim.h"
#include "panel.h"

#define SIZE 8

#define s ((((((((((((((((0

#ifdef TWO
#define X << 1) + 1)
#define O << 1))

static int source[] = { s O O O X X O O O,
                        s O X X X X X X O,
                        s O X X X X X X O,
                        s X X X X X X X X,
                        s X X X X X X X X,
                        s O X X X X X X O,
                        s O X X X X X X O,
                        s O O O X X O O O};
#else
#define X  1,
#define x  2,
#define O  0,

static char source[] = {O O x X X x O O
                        O x X X X X x O
                        x X X X X X X x
                        X X X X X X X X
                        X X X X X X X X
                        x X X X X X X x
                        O x X X X X x O
                        O O x X X x O O};
#endif

GdkPixbuf *Lamps[4];      /* Pixbufs for button state indicators. */

static GdkPixbuf *get_pixbuf(guint pixel)
{
    guint      p;
    guint     *data, *fill;
    int        i, val;

    data = malloc(SIZE * SIZE * sizeof (guint));
#ifdef TWO
    for (fill = data, i = 0; i < SIZE; ++i) {
        int        j;
        
        for (val = source[i], j = 0; j < SIZE; ++j) {
            p = (val & 1) ? (pixel | 0xff000000) : 0;
            val >>= 1;
            *fill++ = p;
        }
    }
#else
    for (fill = data, i = 0; i < SIZE * SIZE; ++i) {
        val = source[i];
        switch (val) {
        case 0:
        default:
            p = 0;
            break;
        case 1:
            p = (pixel | 0xff000000);
            break;
        case 2:
            p = (pixel | 0x80000000); /* Alpha 50% */
            break;
        }
        *fill++ = p;
    }
#endif
    return gdk_pixbuf_new_from_data((guchar *)data, GDK_COLORSPACE_RGB, TRUE,
                                    8, SIZE, SIZE, SIZE * sizeof (guint),
                                    NULL, NULL);
}

void Init_lights(void)
{
    Lamps[BLACK] = get_pixbuf(0);
    Lamps[RED] = get_pixbuf(0xFF);
    Lamps[GREEN] = get_pixbuf(0xFF00);
    Lamps[BLUE] = get_pixbuf(0xFF0000);
}

