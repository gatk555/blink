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

static GtkWidget *thing_to_widget(struct thing *thing, gboolean bare);

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

static GtkWidget *vbox1; /* FIX ME! */

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

/* Set a button's lamp. */

static void set_light(struct reg *this, int index)
{
    int          colour_base;
    GdkPixbuf   *pb;
    GtkWidget   *child;

    if ((this->options & RO_ALT_COLOURS) && ((this->u_flags >> index) & 1))
        colour_base = 2;
    else
        colour_base = 0;
    pb = Lamps[colour_base + ((this->u_value >> index) & 1)];
    child = gtk_button_get_image(GTK_BUTTON(this->u.b.buttons[index]));
    gtk_image_set_from_pixbuf(GTK_IMAGE(child), pb);
}

/* Set a register's visible value. */

static void set_reg(struct reg *this)
{
    unsigned int  i, changed, style;
    int           index;
    gchar         buff[64];

    style = this->options & RO_STYLE_MASK;
    switch (style) {
    case RO_STYLE_BITS:
        /* Set individual bits. */

        /* Open bug: with the simavr/blink example program updating PORTD bits
         * fails at high simulation speed.  Uncommenting thse lines fixes
         * the Heisenbug.
         *
         * printf("Reg %s: old %#x new %#x\n",
         *        this->name, this->u.b.prev_value, this->u_value);
         */

        changed = this->u_value ^ this->u.b.prev_value;
        if (this->options & RO_ALT_COLOURS)
            changed |= this->u_flags ^ this->u.b.prev_flags;
        for (i = 0; i < this->width; ++i, changed >>= 1) {
            if (changed & 1)
                set_light(this, i);
        }
        this->u.b.prev_value = this->u_value;
        this->u.b.prev_flags = this->u_flags;
        return;
    case RO_STYLE_HEX:
        snprintf(buff, sizeof buff, "%1$.*2$X",
                 this->u_value, this->u_max_len);
        break;
    case RO_STYLE_DECIMAL:
    case RO_STYLE_SPIN:
        snprintf(buff, sizeof buff, "%d", this->u_value);
        break;
    case RO_STYLE_COMBO:
        if (this->u_value >= this->u_max_len)
            index = -1;
        else
            index = (int)this->u_value;
        gtk_combo_box_set_active((GtkComboBox *)this->u_entry, index);
        return;
        break;
    case RO_STYLE_FP:
    case RO_STYLE_FP_SPIN:
        snprintf(buff, sizeof buff, "%.*g", this->width, this->fp_value);
        break;
    default:
        return;
    }
    gtk_entry_set_text((GtkEntry *)this->u_entry, buff);
}

static void show_value(struct reg *rp)
{
    struct reg   *cp;
    unsigned int  is_fp, type, value;
    double        f_value;

    type = (rp->options & RO_STYLE_MASK);
    is_fp = (type == RO_STYLE_FP || type == RO_STYLE_FP_SPIN);
    if (is_fp)
        f_value = rp->fp_value;
    else
        value = rp->u_value;

    cp = rp;
    do {
        if (is_fp)
            cp->fp_value = f_value;
        else
            cp->u_value = value;
        set_reg(cp);
        cp = cp->clones;
    } while (cp != rp);
}

/* Send a changed register value to the simulator. */

static void send_new_value(struct reg *this)
{
    g_mutex_lock(&Simulation_mutex);
    if (this->state != User) {
        if (this->state == Simulation) {
            fprintf(stderr, "Overriding simulator value %#x for %s\n",
                   this->u_value, this->name);
        }
        this->state = User;             /* Override simulation. */
        this->chain = User_modified_regs;
        User_modified_regs = this;
    }
    g_mutex_unlock(&Simulation_mutex);

    /* Propagate new value to clones. */

    show_value(this);
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

    this->u_value ^= 1 << index;          /* Flip bit. */
    send_new_value(this);
}

/* Callback for enter in a writeable text widget. */

static void entry_activate(GtkWidget *widget, gpointer data)
{
    struct reg   *this;
    const gchar  *text;
    unsigned int  type, is_fp, value, count, eaten;
    double        f_value;

    this = (struct reg *)data;
    text = gtk_entry_get_text((GtkEntry *)this->u_entry);
    type = (this->options & RO_STYLE_MASK);
    is_fp = (type == RO_STYLE_FP || type == RO_STYLE_FP_SPIN);
    switch (type) {
    case RO_STYLE_HEX:
        eaten = sscanf(text, "%x %n", &value, &count);
        break;
    case RO_STYLE_FP:
    case RO_STYLE_FP_SPIN:
        eaten = sscanf(text, "%lg %n", &f_value, &count);
        break;
    default: // Decimal
        eaten = sscanf(text, "%u %n", &value, &count);
        break;
    }
    if (eaten != 1 || text[count]) {
        /* Bad value.  Blank it and return. */

        gtk_entry_set_text((GtkEntry *)this->u_entry, "");
        return;
    }

    if (is_fp) {
        if (this->fp_value == f_value)
            return;
        this->fp_value = f_value;
    } else {
        if (this->u_value == value)
            return;
        this->u_value = value;
    }
    send_new_value(this);
}

/* Callback for new value in a spin button widget. */

static void spin_reg_new_value(GtkWidget *widget, gpointer data)
{
    struct reg    *this;
    GtkSpinButton *button;
    unsigned int   value;

    this = (struct reg *)data;
    button = GTK_SPIN_BUTTON(this->u_entry);
    value = gtk_spin_button_get_value_as_int(button);
    if (this->u_value == value)
        return;
    this->u_value = value;
    send_new_value(this);
}

/* Callback for new value in a spin button widget. */

static void spin_reg_new_fp_value(GtkWidget *widget, gpointer data)
{
    struct reg    *this;
    GtkSpinButton *button;
    double         value;

    this = (struct reg *)data;
    button = GTK_SPIN_BUTTON(this->u_entry);
    value = gtk_spin_button_get_value(button);
    if (this->fp_value == value)
        return;
    this->fp_value = value;
    send_new_value(this);
}

/* Callback for new combo-box selection. */

static void combo_changed(GtkWidget *widget, gpointer data)
{
    struct reg   *this;
    GtkComboBox  *combo;
    unsigned int  value;

    this = (struct reg *)data;
    combo = GTK_COMBO_BOX(this->u_entry);
    value = gtk_combo_box_get_active(combo);
    if (this->u_value == value)
        return;
    this->u_value = value;
    send_new_value(this);
}

/* This function is called when the simulation has new values to display. */

gboolean Simulation_call(gpointer data)
{
    struct reg   *rp;

    rp = (struct reg *)data;
    if (!rp)
        return FALSE;
    g_mutex_lock(&Simulation_mutex);
    if (rp->state == Simulation) {
        rp->state = Valid;
        show_value(rp);
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
        value = rp->u_flags;
        do {
            cp->u_flags = value;
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

    clock_p = (struct clock *)data;
    clock_p->fast = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    if (clock_p->fast && clock_p->cycles_fast == 0)
        clock_p->cycles_fast = clock_p->cycles_slow;
    Display_burst(NULL);
}

/* Key press callback for top level. */

static gboolean key_cb(GtkWidget *UNUSED(widget), GdkEventKey *event,
                       gpointer data)
{
    switch (event->keyval) {
    case GDK_KEY_f:
    case GDK_KEY_F:
        click_fast(NULL, data);
        break;
    case GDK_KEY_g:
    case GDK_KEY_G:
        click_go(NULL, data);
        break;
    case GDK_KEY_q:
    case GDK_KEY_Q:
        stop();
        break;
    case GDK_KEY_r:
    case GDK_KEY_R:
        click_toggle(NULL, &The_clock.run);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

#ifdef QUIT_BUTTON
static void do_quit(GtkWidget *UNUSED(widget), gpointer UNUSED(data))
{
    stop();
}
#endif

/* Callback for combo-box selection: simulator units. */

static void unit_changed(GtkWidget *widget, gpointer data)
{
    unsigned int unit;

    unit = gtk_combo_box_get_active(The_clock.combo);
    if (unit != The_clock.unit) {
        The_clock.unit = unit;

        /* Send the new value as a dummy register change. */

        The_clock.unit_reg.u_value = unit;
        send_new_value(&The_clock.unit_reg);
    }
}

/* Callback for "Burst" spin-box. */

static void cycles_new_value(GtkWidget *spin, void *param)
{
    gint           new;

    new = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
    if (The_clock.sim_ctl)
        The_clock.cycles_sim = new;
    else if (The_clock.fast)
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

/* Change the displayed burst value when the run mode changes.
 * Called by gtk_main() as an idle-time function.
 */

gboolean Display_burst(gpointer data)
{
    unsigned int   val;
    gchar          buff[32];

    if (The_clock.sim_ctl)
        val = The_clock.cycles_sim;
    else if (The_clock.fast)
        val = The_clock.cycles_fast;
    else
        val = The_clock.cycles_slow;
    snprintf(buff, sizeof buff, "%u", val);
    gtk_entry_set_text(GTK_ENTRY(The_clock.burst), buff);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* Change the visible item in an overlayed display area.
 * Called by gtk_main() as an idle-time function.
 */

gboolean Overlay_switch(gpointer data)
{
    struct overlay *this;

    this = (struct overlay *)data;     /* Recover type. */
    gtk_stack_set_visible_child(this->stack, this->items[this->choice]);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* Set a new strings table for a combo-box register.
 * Called by gtk_main() as an idle-time function.
 */

gboolean New_strings_call(gpointer data)
{
    GtkComboBoxText *it;
    struct reg      *this;
    const char      * const *strings;
    unsigned int     type, count;

    this = (struct reg *)data;     /* Recover type. */
    type = this->options & RO_STYLE_MASK;
    if (type != RO_STYLE_COMBO)
         return FALSE;

    it = GTK_COMBO_BOX_TEXT(this->u_entry);
    gtk_combo_box_text_remove_all(it);

    g_mutex_lock(&Simulation_mutex);
    strings = this->u.e.strings;
    for (count = 0; *strings; ++strings, ++count)
        gtk_combo_box_text_append_text(it, *strings);
    g_mutex_unlock(&Simulation_mutex);
    gtk_combo_box_set_active((GtkComboBox *)it, 0);

    this->u_max_len = count;
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* End of run-time code, start of simulator-side setup functions. */

/* Create a new text entry widget or spin button to display a register. */

static GtkWidget *raw_reg_entry_new(struct reg *this)
{
    GtkWidget     *entry;
    GtkAdjustment *adj;
    unsigned int   max_len, type;

    type = this->options & RO_STYLE_MASK;
    switch (type) {
    case RO_STYLE_HEX:
        max_len = (this->width + 3) >> 2;
        break;
    case RO_STYLE_FP:
    case RO_STYLE_FP_SPIN:
        max_len = this->width + 7; // 2 signs, point, e, 3digits
        break;
    case RO_STYLE_COMBO:
        max_len = 0;
        break;
    default: // Decimal
        max_len = (this->width + 2) / 3;
        while (pow(10.0, max_len - 1) > pow(2.0, this->width) - 1)
            --max_len;
        break;
    }
    this->u_max_len = max_len;
    ++max_len;          // Allow for slight misalignment.

    switch (type) {
    case RO_STYLE_SPIN:
        adj = (GtkAdjustment *)
                  gtk_adjustment_new(0.0, 0.0,
                                     pow(2.0, (double)this->width) - 1,
                                     1.0, 1.0, 0.0);
        entry = gtk_spin_button_new(adj, 0.8, 0);
        g_signal_connect(entry, "value-changed",
                         G_CALLBACK(spin_reg_new_value), this);
        gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(entry), FALSE);
        break;
    case RO_STYLE_FP_SPIN:
        adj = (GtkAdjustment *)gtk_adjustment_new(0.0, -DBL_MAX, DBL_MAX,
                                                  0.1, 0.0, 0.0);
        entry = gtk_spin_button_new(adj, 20.0, this->width - 1);
        g_signal_connect(entry, "value-changed",
                         G_CALLBACK(spin_reg_new_fp_value), this);
        gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(entry), FALSE);
        break;
    case RO_STYLE_COMBO:
        entry = gtk_combo_box_text_new();
        g_signal_connect(entry, "changed", G_CALLBACK(combo_changed), this);
        break;
    default:
        entry = gtk_entry_new();
        g_signal_connect(entry, "activate", G_CALLBACK(entry_activate), this);
        break;
    }

    this->u_entry = entry;
    if (this->options & RO_INSENSITIVE)
        gtk_widget_set_sensitive(entry, FALSE);

    if (type != RO_STYLE_COMBO) {
        gtk_entry_set_alignment((GtkEntry *)entry, ALIGNMENT);  // Align right.
        gtk_entry_set_max_length((GtkEntry *)entry, (gint)max_len);
        gtk_entry_set_width_chars((GtkEntry *)entry, (gint)max_len);
        gtk_entry_set_max_width_chars((GtkEntry *)entry, (gint)max_len);
        set_reg(this);
    }
    gtk_widget_show(entry);
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

static GtkWidget *reg_new(struct reg *this, gboolean bare)
{
    GtkWidget  *it, *raw;

    raw = raw_reg_new(this);
    if (bare || !this->name) {
        it = raw;
    } else {
        it = gtk_frame_new(this->name);
        gtk_container_add(GTK_CONTAINER(it), raw);
    }
    return it;
}

/* Return a name for a thing. */

static char *thing_to_name(struct thing *it)
{
    char *n;

    switch (it->type) {
    case Register:
        n = it->u.reg.name;
        break;
    case Row:
        n = it->u.row.name;
        break;
    case Overlay:
        n = it->u.overlay.name;
        break;
    default:
        n = NULL;
    }
    return n;
}

/* Create a named row of visible items. */

static GtkWidget *row_new(struct row *this, gboolean bare)
{
    GtkWidget    *it, *hbox, *reg, *lbl;
    char         *name;
    int           i;

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    if (bare) {
        it = hbox;
    } else {
        it = gtk_frame_new(this->name);
        gtk_container_add(GTK_CONTAINER(it), hbox);
        gtk_widget_show(hbox);
    }

    /* Loop over the list of widgets.
     * The box is filled from the right, like registers, so labels
     * are packed after the register.
     */

    for (i = 0; i < this->count; ++i) {
        struct thing *thing;

        thing = this->items[i];
        name = thing_to_name(thing);
        reg = thing_to_widget(thing, TRUE);
        gtk_box_pack_end(GTK_BOX(hbox), reg, FALSE, FALSE, 0);
        gtk_widget_show(reg);

        if (name) {
            lbl = gtk_label_new(name);
            gtk_box_pack_end(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
            gtk_widget_show(lbl);
        }
    }
    return it;
}

/* Create a named grid of visible items. */

static GtkWidget *grid_new(struct grid *this, gboolean bare)
{
    GtkWidget    *it, *grid, *hbox, *reg, *lbl;
    char         *name;
    int           i, row, col;

    grid = gtk_grid_new();
    if (bare) {
        it = grid;
    } else {
        it = gtk_frame_new(this->name);
        gtk_container_add(GTK_CONTAINER(it), grid);
        gtk_widget_show(grid);
    }

    if (this->columns < 0)
        this->columns = -this->columns;
    else
        gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    /* Loop over the list of registers.
     * The box is filled from the right, like registers, so labels
     * are packed after the register.
     */

    for (i = 0; i < this->count; ++i) {
        struct thing *thing;

        thing = this->items[i];
        name = thing_to_name(thing);
        reg = thing_to_widget(thing, TRUE);
        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_end(GTK_BOX(hbox), reg, FALSE, FALSE, 0);
        gtk_widget_show(reg);

        if (name) {
            lbl = gtk_label_new(name);
            gtk_box_pack_end(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
            gtk_widget_show(lbl);
        }

        gtk_container_set_border_width(GTK_CONTAINER(hbox), 2);
        col = i % this->columns;
        row = i / this->columns;
        gtk_grid_attach(GTK_GRID(grid), hbox, col, row, 1, 1);
        gtk_widget_show(hbox);
    }
    return it;
}

/* Create a new displayed item. */

static GtkWidget *thing_to_widget(struct thing *thing, gboolean bare)
{
    struct reg     *reg;
    struct row     *row;
    struct grid    *grid;
    GtkWidget      *it;

    switch (thing->type) {
    case Register:
        reg = &thing->u.reg;
        it = reg_new(reg, bare);
        break;
    case Row:
        row = &thing->u.row;
        it = row_new(row, bare);
        free(thing); // No longer needed.
        break;
    case Grid:
        grid = &thing->u.grid;
        it = grid_new(grid, bare);
        free(thing); // No longer needed.
        break;
    case Overlay:
        {
            struct overlay *overlay;
            struct thing   *item;
            GtkWidget      *w;
            char           *name;
            int             i;
            char            buff[32];

            overlay = &thing->u.overlay;
            it = gtk_stack_new();
            overlay->stack = GTK_STACK(it);
            for (i = 0; i < overlay->count; ++i) {
                item = (struct thing *)overlay->items[i];
                w = thing_to_widget(item, FALSE);
                overlay->items[i] = w;
                name = thing_to_name(item);
                if (!name) {
                    sprintf(buff, "%d", i); // Make a unique name
                    name = buff;
                }
                gtk_stack_add_named(overlay->stack, w, name);
            }
        }
        break;
    default:
        abort();        // Unknown thing.
        break;
    }
    gtk_widget_show(it);
    return it;
}

/* The simulator has a new item to display.
 * Called by gtk_main() as an idle-time function.
 */

gboolean New_thing_call(gpointer data)
{
    struct thing *thing;

    thing = (struct thing *)data;     /* Recover type. */

    /* Add the display widget. */

    gtk_box_pack_start(GTK_BOX(vbox1), thing_to_widget(thing, FALSE),
                       FALSE, FALSE, 0);
    return FALSE;       /* Tell Glib loop we are finished. */
}

/* End of registers, start of clock. */

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
    GtkWidget          *it, *hbox, *combo;

    The_clock.run = 0;
    The_clock.go = 0;
    The_clock.cycles_fast = 0;
    The_clock.cycles_slow = 1;
    The_clock.cycles_sim = 1;
    The_clock.rate = 20;    /* 2Hz. */

    it = gtk_frame_new("Clock");
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(it), hbox);

    The_clock.run_button = add_toggle("_Run", NULL, &The_clock.run, hbox);
    add_button("_Go", click_go, &The_clock, hbox);
    The_clock.burst = add_spin("Burst", cycles_new_value,
                               &The_clock.cycles_slow, 12, hbox);

    /* Add "units" combo box, initially hidden. */

    combo = gtk_combo_box_text_new();
    g_signal_connect(combo, "changed", G_CALLBACK(unit_changed), NULL);
    The_clock.combo = GTK_COMBO_BOX(combo);
    gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);

    /* Set-up dummy register structure for clock combo-box. */

    The_clock.unit_reg.handle = COMBO_HANDLE;        // Dummy handle
    The_clock.unit_reg.u_entry = combo;
    The_clock.unit_reg.options = RO_STYLE_COMBO;
    The_clock.unit_reg.clones = &The_clock.unit_reg; // Initialise list.

    add_toggle("_Fast", click_fast, &The_clock, hbox);
    add_spin("Speed", NULL, &The_clock.rate, 6, hbox);

    gtk_widget_show(hbox);
    gtk_widget_show(it);
    return it;
}

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

/* Create window and clock. */

static void build_ui(const char * title)
{
    GtkWidget *window;
    GtkWidget *it;
     
    Init_lights();
    setup_style();
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_event), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy_cb), NULL);

    /* Request key down events. */

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(window, "key_press_event",
                     G_CALLBACK(key_cb), &The_clock);

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

void Start_Panel(const char * title,
                 const char **unit_strings, unsigned int initial_unit)
{
    static int   argc;

    /* build_ui() must be called before starting simulation. */

    gtk_init(&argc, NULL);
    build_ui(title);
    if (unit_strings) {
        int count;

        for (count = 0; *unit_strings; ++unit_strings, ++count)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(The_clock.combo),
                                           *unit_strings);
        The_clock.unit_reg.u_max_len = count;
        gtk_combo_box_set_active(The_clock.combo, initial_unit);
        gtk_widget_show((GtkWidget *)The_clock.combo);
    }
    g_thread_new("Panel UI thread", panel_thread, NULL);
}
