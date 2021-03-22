#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <inttypes.h>
#include <gtk/gtk.h>
#include <glib-object.h>

#include "sim.h"
#include "panel.h"

#if defined(QUIT_BUTTON) && !defined(WINDOW_HEADING)
#define WINDOW_HEADING
#endif

#define WIDTH        8          /* Default bit width. */
#define BIT_KEY    "n"          /* Key for widget-associated items. */

/* Right-align numeric text in entry boxes, but a little margin makes
 * it much easier to get the cursor into the right end.
 */

#define ALIGNMENT 0.95

/* Global data items. */

struct clock    The_clock;

/* List of struct_regs with pending simulator updates - mutex locked. */

struct reg     *User_modified_regs;

/* Locking for the above. */

GMutex          Simulation_mutex;

/* To wake sleeping simulation thread. */

GCond           Simulation_waker;

/* If not NULL, this container is used for new items. */

static struct overlayed_regs *Current_overlay;

/* Window delete event handler for top-level. */

static gboolean delete_event(GtkWidget *UNUSED(widget),
                             GdkEvent  *UNUSED(event), gpointer UNUSED(dp))
{
    return FALSE;       /* Requests window destruction. */
}

/* Force main loop and program exit. */

static void stop(void)
{

    g_mutex_lock(&Simulation_mutex);
    User_modified_regs = EXIT_VALUE; // Inform simulator.
    g_mutex_unlock(&Simulation_mutex);
    g_thread_exit(NULL);
}

/* Window destruction callback for top level. */

static void destroy_cb(GtkWidget *UNUSED(widget), gpointer UNUSED(dp))
{
    stop();
}

/* End main window, start register. */

/* Remove padding around the lamp in per-bit buttons and provide
 * a narrow margin below to centre vertically.
 */

#define CSS \
    "button { padding: 0px; margin: 0px 0px 6px 0px; }\n" \
    "frame { padding: 4px; background-color: #ddd; }\n"

static void setup_style(void)
{
    GtkCssProvider *provider;
    GdkDisplay *display;
    GdkScreen *screen;

    provider = gtk_css_provider_new();
    display = gdk_display_get_default();
    screen = gdk_display_get_default_screen(display);

    gtk_css_provider_load_from_data(GTK_CSS_PROVIDER(provider), CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
         screen,
         GTK_STYLE_PROVIDER(provider),
         GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* Set a button's lamp. */

static void set_light(struct reg *this, int index)
{
    int          colour_base;
    GdkPixbuf   *pb;
    GtkWidget   *child;

    if ((this->options & RO_ALT_COLOURS) && ((this->flags >> index) & 1))
        colour_base = 2;
    else
        colour_base = 0;
    pb = Lamps[colour_base + ((this->value >> index) & 1)];
    child = gtk_button_get_image(GTK_BUTTON(this->u.b.buttons[index]));
    gtk_image_set_from_pixbuf(GTK_IMAGE(child), pb);
}

/* Set a register's visible value. */

static void set_reg(struct reg *this)
{
    unsigned int  i, changed, style;

    style = this->options & RO_STYLE_MASK;
    if (style) {
        gchar buff[32];

        /* Text entry widget. */

        snprintf(buff, sizeof buff,
                 style == RO_STYLE_HEX ? "%1$.*2$X" : "%d",
                 this->value, this->u.e.max_len);
        gtk_entry_set_text((GtkEntry *)this->u.e.entry, buff);
        return;
    }
                 
    /* Set individual bits. */

    /* Open bug: with the simavr/blink example program updating PORTD bits
     * fails at high simulation speed.  Uncommenting thse lines fixes
     * the Heisenbug.
     *
     * printf("Reg %s: old %#x new %#x\n",
     *        this->name, this->u.b.prev_value, this->value);
     */

    changed = this->value ^ this->u.b.prev_value;
    if (this->options & RO_ALT_COLOURS)
        changed |= this->flags ^ this->u.b.prev_flags;
    for (i = 0; i < this->width; ++i, changed >>= 1) {
        if (changed & 1)
            set_light(this, i);
    }
    this->u.b.prev_value = this->value;
    this->u.b.prev_flags = this->flags;
}

/* Send a changed register value to the simulator. */

static void send_new_value(struct reg *this)
{
    struct reg          *clone;
    unsigned int         value;

    g_mutex_lock(&Simulation_mutex);
    if (this->state != User) {
        if (this->state == Simulation) {
            fprintf(stderr, "Overriding simulator value %#x for %s\n",
                   this->value, this->name);
        }
        this->state = User;             /* Override simulation. */
        this->chain = User_modified_regs;
        User_modified_regs = this;
    }
    g_mutex_unlock(&Simulation_mutex);

    /* Propagate new value to clones. */

    value = this->value;
    clone = this;
    do {
        clone->value = value;
        set_reg(clone);
        clone = clone->clones;
    } while (clone != this);
}

/* Callback for clicking a register button. */

static void click_bit(GtkWidget *widget, gpointer data)
{
    struct reg          *this;
    unsigned int         index;

    /* Find which button was pressed. */

    this = (struct reg *)data;
    index = (uintptr_t)g_object_get_data(G_OBJECT(widget), BIT_KEY);
    if (index >= this->width)
        return;                         /* Never taken. */

    this->value ^= 1 << index;          /* Flip bit. */
    send_new_value(this);
}

/* Callback for enter in a writeable text widget. */

static void entry_activate(GtkWidget *widget, gpointer data)
{
    struct reg   *this;
    const gchar  *text;
    gboolean      is_hex;
    unsigned int  value, count;

    this = (struct reg *)data;
    text = gtk_entry_get_text((GtkEntry *)this->u.e.entry);
    is_hex = (this->options & RO_STYLE_MASK) == RO_STYLE_HEX;
    if (sscanf(text, is_hex ? "%x %n" : "%u %n", &value, &count) != 1 ||
        text[count]) {
        /* Bad value.  Blank it and return. */

        gtk_entry_set_text((GtkEntry *)this->u.e.entry, "");
        return;
    }
    this->value = value;
    send_new_value(this);
}

/* Callback for new value in a spin button widget. */

static void spin_reg_new_value(GtkWidget *widget, gpointer data)
{
    struct reg    *this;
    GtkSpinButton *button;
    unsigned int   value;

    this = (struct reg *)data;
    button = GTK_SPIN_BUTTON(this->u.e.entry);
    value = gtk_spin_button_get_value_as_int(button);
    this->value = value;
    send_new_value(this);
}

/* This function is called when the simulation has new values to display. */

gboolean Simulation_call(gpointer data)
{
    struct reg   *rp, *cp;
    unsigned int  value;

    rp = (struct reg *)data;
    if (!rp)
        return FALSE;
    g_mutex_lock(&Simulation_mutex);
    if (rp->state == Simulation) {
        rp->state = Valid;
        cp = rp;
        value = rp->value;
        do {
            cp->value = value;
            set_reg(cp);
            cp = cp->clones;
        } while (cp != rp);
    } else {
        fprintf(stderr, "In Simulation_call for %s but state %d\n",
                rp->name, rp->state);
    }
    g_mutex_unlock(&Simulation_mutex);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* This function is called when the simulation has new display flags. */

gboolean Flag_call(gpointer data)
{
    struct reg   *rp, *cp;
    unsigned int  value;
    unsigned int  bits, i;

    rp = (struct reg *)data;
    if (!rp)
        return FALSE;
    g_mutex_lock(&Simulation_mutex);
    if (rp->state == Simulation) {
        rp->state = Valid;
        cp = rp;
        value = rp->flags;
        do {
            cp->flags = value;
            set_reg(cp);
            if (cp->options & RO_SENSITIVITY) {
                for (i = 0, bits = 1; i < cp->width; ++i, bits <<= 1) {
                    gtk_widget_set_sensitive(cp->u.b.buttons[i],
                                             (bits & value) != 0);
                }
            }
            cp = cp->clones;
        } while (cp != rp);
    } else {
        fprintf(stderr, "In Flag_call for %s but state %d\n",
                rp->name, rp->state);
    }
    g_mutex_unlock(&Simulation_mutex);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* Simulator stopped, probably on request. */

void Blink_stopped(void)
{
    gtk_toggle_button_set_active(The_clock.run_button, FALSE);
}

/* Create a new text entry widget or spin button to display a register. */

static GtkWidget *raw_reg_entry_new(struct reg *this)
{
    GtkWidget     *entry;
    GtkAdjustment *adj;
    gboolean       is_hex;
    unsigned int   max_len;

    is_hex = (this->options & RO_STYLE_MASK) == RO_STYLE_HEX;
    if (is_hex) {
        max_len = (this->width + 3) >> 2;
    } else {
        max_len = (this->width + 2) / 3;
        while (pow(10.0, max_len - 1) > pow(2.0, this->width) - 1)
            --max_len;
    }
    this->u.e.max_len = max_len;
    ++max_len;          // Allow for slight misalignment.

    if ((this->options & RO_STYLE_MASK) == RO_STYLE_SPIN) {
        adj = (GtkAdjustment *)
                  gtk_adjustment_new(0.0, 0.0,
                                     pow(2.0, (double)this->width) - 1,
                                     1.0, 1.0, 0.0);
        entry = gtk_spin_button_new(adj, 0.8, 0);
        g_signal_connect(entry, "value-changed",
                         G_CALLBACK(spin_reg_new_value), this);
        gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(entry), FALSE);
    } else {
        entry = gtk_entry_new();
        g_signal_connect(entry, "activate", G_CALLBACK(entry_activate), this);
    }

    this->u.e.entry = entry;
    if (this->options & RO_INSENSITIVE)
        gtk_widget_set_sensitive(entry, FALSE);
    gtk_entry_set_alignment((GtkEntry *)entry, ALIGNMENT);    // Align right.
    gtk_entry_set_max_length((GtkEntry *)entry, (gint)max_len);
    gtk_entry_set_width_chars((GtkEntry *)entry, (gint)max_len);
    gtk_entry_set_max_width_chars((GtkEntry *)entry, (gint)max_len);
    gtk_widget_show(entry);
    set_reg(this);
    return entry;
}

/* Create the guts of a visible register with individual bits. */

static GtkWidget *raw_reg_new(struct reg *this)
{
    GtkWidget    *hbox, *ibox, *but;
    unsigned int  i;
    int           j;

    if (this->options & RO_STYLE_MASK) {
        /* For now assume a text entry widget. */

        return raw_reg_entry_new(this);
    }
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    /* Make a row of buttons, in groups of 4. */

    for (i = 0; i < this->width; ) {
        ibox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_end(GTK_BOX(hbox), ibox, FALSE, FALSE, 0);

        /* Make a group of 4 buttons. */

        for (j = 0; j < 4; ++j, ++i) {
            if (i >= this->width)
                break;

            but = gtk_button_new();
            if (this->options & RO_INSENSITIVE)
                gtk_widget_set_sensitive(but, FALSE);
            this->u.b.buttons[i] = but;

            gtk_box_pack_end(GTK_BOX(ibox), but, FALSE, FALSE, 0);
            gtk_widget_show(but);

            /* Store the current bit index on the button, to be used by
             * the signal's callback function.
             */

            g_object_set_data(G_OBJECT(but), BIT_KEY, (gpointer)(intptr_t)i);
            g_signal_connect(but, "clicked", G_CALLBACK(click_bit), this);
            gtk_button_set_image(GTK_BUTTON(but),
                                 gtk_image_new_from_pixbuf(Lamps[BLUE]));
            set_light(this, i);
        }
        gtk_widget_show(ibox);
    }
    gtk_widget_show(hbox);
    set_reg(this);
    return hbox;
}

/* Create a new visible register. */

static GtkWidget *reg_new(struct reg *this)
{
    GtkWidget  *it;

    it = gtk_frame_new(this->name);
    gtk_container_add(GTK_CONTAINER(it), raw_reg_new(this));
    gtk_widget_show(it);
    return it;
}

/* Create a named row of visible registers. */

static GtkWidget *row_new(struct row *this)
{
    GtkWidget  *it, *hbox, *reg, *lbl;
    int         i;

    it = gtk_frame_new(this->name);
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(it), hbox);

    /* Loop over the list of registers.
     * The box is filled from the right, like registers, so labels
     * are packed after the register.
     */

    for (i = 0; i < this->count; ++i) {
        reg = raw_reg_new(this->regs[i]);
        gtk_box_pack_end(GTK_BOX(hbox), reg, FALSE, FALSE, 0);
        gtk_widget_show(reg);

        lbl = gtk_label_new(this->regs[i]->name);
        gtk_box_pack_end(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
        gtk_widget_show(lbl);
    }
    gtk_widget_show(hbox);
    gtk_widget_show(it);

    /* The calls to raw_reg_new() copied all the struct reg pointers
     * in *this, so it can now be freed.
     */

    free(this);
    return it;
}

/* End of registers, start of clock. */

/* Something changed, wake the simulation thread. */

static void wake_simulation(void)
{
    g_cond_signal(&Simulation_waker);
}

static void click_toggle(GtkWidget *UNUSED(widget), gpointer data)
{
    unsigned int        *var;

    var = (unsigned int *)data;
    *var ^= 1;
    wake_simulation();
}

static void click_go(GtkWidget *UNUSED(widget), gpointer data)
{
    struct clock       *clock_p;

    clock_p = (struct clock *)data;
    clock_p->go = 1;
    wake_simulation();
}

static void click_fast(GtkWidget *widget, gpointer data)
{
    struct clock  *clock_p;
    gchar          buff[32];

    clock_p = (struct clock *)data;
    clock_p->fast = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    if (clock_p->fast && clock_p->cycles_fast == 0)
        clock_p->cycles_fast = clock_p->cycles_slow;
    snprintf(buff, sizeof buff, "%u",
             clock_p->fast ? clock_p->cycles_fast : clock_p->cycles_slow);
    gtk_entry_set_text(GTK_ENTRY(clock_p->burst), buff);
}

#ifdef QUIT_BUTTON
static void do_quit(GtkWidget *UNUSED(widget), gpointer UNUSED(data))
{
    stop();
}
#endif

/* Callback for "Burst" spin-box. */

static void cycles_new_value(GtkWidget *spin, void *param)
{
    gint           new;

    new = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
    if (The_clock.fast)
        The_clock.cycles_fast = new;
    else
        The_clock.cycles_slow = new;
    wake_simulation();
}

/* Callback to track spin-box changes. */

static void spin_new_value(GtkSpinButton *spin, unsigned int *var)
{
    *var = gtk_spin_button_get_value_as_int(spin);
    wake_simulation();
}

/* Make a spin button.  Argument "var", for initial value, must not be null. */

static GtkSpinButton *add_spin(const char *name,
                               GtkCallback action, unsigned int *var,
                               unsigned int max_width, GtkWidget *box)
{
    static GtkAdjustment *adj;
    GtkWidget            *ibox, *label, *spin;

    ibox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), ibox, TRUE, FALSE, 0);

    label = gtk_label_new(name);
    gtk_box_pack_start(GTK_BOX(ibox), label, FALSE, FALSE, 0);
    gtk_widget_show(label);

    adj = (GtkAdjustment *)gtk_adjustment_new(*var, 1.0, 1.0e6,
                                              1.0, 100.0, 0.0);
    spin = gtk_spin_button_new(adj, 0.8, 0);
    if (action == NULL)
        action = (GtkCallback)spin_new_value;
    g_signal_connect(spin, "value-changed", G_CALLBACK(action), var);
    gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (spin), FALSE);
    gtk_entry_set_alignment((GtkEntry *)spin, ALIGNMENT);    // Align right.
    gtk_entry_set_width_chars((GtkEntry *)spin, (gint)max_width);
    gtk_entry_set_max_width_chars((GtkEntry *)spin, (gint)max_width);
    gtk_box_pack_start(GTK_BOX(ibox), spin, FALSE, FALSE, 0);
    gtk_widget_show(spin);
    gtk_widget_show(ibox);
    return GTK_SPIN_BUTTON(spin);
}

/* Create and return a toggle button. */

static GtkToggleButton *add_toggle(const char *name,
                                   GtkCallback action, void *var,
                                   GtkWidget *box)
{
    GtkWidget  *but;

    but = gtk_toggle_button_new_with_mnemonic(name);
    gtk_box_pack_start(GTK_BOX(box), but, TRUE, FALSE, 0);
    gtk_widget_show(but);
    if (action == NULL)
        action = (GtkCallback)click_toggle;
    g_signal_connect(but, "clicked", G_CALLBACK(action), var);
    return (GtkToggleButton *)but;
}

/* Create a simple push button rather than a toggle. */

static GtkWidget *add_button(const char  *name,
                             void       (*action)(GtkWidget *, gpointer),
                             gpointer     data,
                             GtkWidget   *hbox)
{
    GtkWidget  *but;

    but = gtk_button_new_with_mnemonic(name);
    gtk_box_pack_start(GTK_BOX(hbox), but, FALSE, FALSE, 4);
    gtk_widget_show(but);
    g_signal_connect(but, "clicked", G_CALLBACK(action), data);
    return but;
}

static GtkWidget *clock_init(void)
{
    GtkWidget          *it, *hbox;

    The_clock.run = 0;
    The_clock.go = 0;
    The_clock.cycles_fast = 0;
    The_clock.cycles_slow = 1;
    The_clock.rate = 20;    /* 2Hz. */

    it = gtk_frame_new("Clock");
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(it), hbox);

    The_clock.run_button = add_toggle("_Run", NULL, &The_clock.run, hbox);
    add_button("_Go", click_go, &The_clock, hbox);
    The_clock.burst = add_spin("Burst", cycles_new_value,
                               &The_clock.cycles_slow, 12, hbox);
    add_toggle("_Fast", click_fast, &The_clock, hbox);
    add_spin("Speed", NULL, &The_clock.rate, 3, hbox);

    gtk_widget_show(hbox);
    gtk_widget_show(it);
    return it;
}

static GtkWidget *vbox1; /* FIX ME! */

/* Make a new register display visible. */

static void add_reg_widget(GtkWidget *it, char *name)
{
    if (Current_overlay) {
        /* Register display is being overlayed by a GtkStack. */

        if (Current_overlay->count >= MAX_OVERLAY_ITEMS) {
            fprintf(stderr, "Too many (%d) items in overlay %s\n",
                    Current_overlay->count, Current_overlay->name);
            exit(1);
        }
        Current_overlay->items[Current_overlay->count++] = it;
        gtk_stack_add_named(Current_overlay->stack, it, name);
    } else {
        gtk_box_pack_start(GTK_BOX(vbox1), it, FALSE, FALSE, 0);
    }
}

/* Create a new register display.
 * Called by gtk_main() as an idle-time function.
 */

gboolean New_reg_call(gpointer data)
{
    GtkWidget   *it;
    struct reg  *this;

    this = (struct reg *)data;     /* Recover type. */

    /* Add the display widget. */

    it = reg_new(this);
    add_reg_widget(it, this->name);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* Create a new display for a row of small registers.
 * Called by gtk_main() as an idle-time function.
 */

gboolean New_row_call(gpointer data)
{
    GtkWidget   *it;
    struct row  *this;
    char        *name;

    this = (struct row *)data;     /* Recover type. */
    name = this->name;
    it = row_new(this);
    add_reg_widget(it, name);
    free(name);         /* From strdup(), 'this' already gone (row_new()). */
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* The argument is a structure describing a display that shows one of a group 
 * of registers or register rows, depending on a value in the simulation.
 * Store the argument and set up the display object if necessary.
 * Called by gtk_main() as an idle-time function.
 */

gboolean Overlay_call(gpointer data)
{
    GtkWidget             *it;
    struct overlayed_regs *this;

    this = (struct overlayed_regs *)data;
    Current_overlay = this;
    if (!this || this->stack)
        return FALSE;

    /* Make the display widget. */

    it = gtk_stack_new();
    this->stack = GTK_STACK(it);
    gtk_widget_show(it);
    gtk_box_pack_start(GTK_BOX(vbox1), it, FALSE, FALSE, 0);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* Change the visible item in an overlayed display area.
 * Called by gtk_main() as an idle-time function.
 */

gboolean Overlay_switch(gpointer data)
{
    struct overlayed_regs *this = (struct overlayed_regs *)data;

    gtk_stack_set_visible_child(this->stack, this->items[this->choice]);
    return FALSE;       /* Tell Glib loop we are finished. */
}

static void build_ui(const char * title)
{
    GtkWidget *window;
    GtkWidget *it;
     
    Init_lights();
    setup_style();
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_event), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy_cb), NULL);

    vbox1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), vbox1);

    if (title && title[0])
        gtk_window_set_title(GTK_WINDOW(window), title);
#ifdef WINDOW_HEADING
    else
        title = "Blink";

    {
        GtkWidget *tbox;

        tbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
#ifdef QUIT_BUTTON
        add_button("_Quit", do_quit, NULL, tbox);
#endif
        it = gtk_label_new(title);
        gtk_box_pack_start(GTK_BOX(tbox), it, TRUE, FALSE, 0);
        gtk_widget_show(it);
#ifdef QUIT_BUTTON
        add_button("_Quit", do_quit, NULL, tbox);
#endif
        gtk_widget_show(tbox);
        gtk_box_pack_start(GTK_BOX(vbox1), tbox, FALSE, FALSE, 0);
    }
#endif

    it = clock_init();
    gtk_box_pack_start(GTK_BOX(vbox1), it, FALSE, FALSE, 0);
    gtk_widget_show(vbox1);
    gtk_widget_show(window);
}

/* Main function for the Gtk UI thread. */

static gpointer panel_thread(gpointer UNUSED(user_data))
{
    gtk_main();         /* Never returns. */
    return NULL;
}

/* Start the UI thread. */

void Start_Panel(const char * title)
{
    static int   argc;

    /* build_ui() must be called before starting simulation. */

    gtk_init(&argc, NULL);
    build_ui(title);
    g_thread_new("Panel UI thread", panel_thread, NULL);
}
