/*
 * Copyright 2021 Giles Atkinson
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <stdio.h>
#include <stdint.h>
#include <glib.h>

#include "sim.h"
#include "no_gtk.h"     /* Really only GtkWidget etc. */
#include "panel.h"

/* Simulator-side interface to the Blink library. */

/* Static (for now) pointer to the simulator's functions. */

static const struct simulator_calls *Sfp;

/* Hash table for Sim_RH handles to display structs. */

static GHashTable *GHt;

int Blink_init(const char                    *title,
               const struct simulator_calls  *calls,
               const char                   **unit_strings,
               unsigned int                   initial_unit)
{
    Sfp = calls;
    GHt = g_hash_table_new(NULL, NULL); /* Hash table gpointer->gpointer. */
    Start_Panel(title, unit_strings, initial_unit);
    return 1;
}

/* Return a name for a thing. */

static const char *nameof(struct thing *it)
{
    const char *n;

    switch (it->type) {
    case Register:
        n = it->u.reg.name;
        if (!n)
            n = "[Unnamed register]";
        break;
    case Row:
        n = it->u.row.name;
        if (!n)
            n = "[Unnamed row]";
        break;
    case Overlay:
        n = it->u.overlay.name;
        if (!n)
            n = "[Unnamed overlay]";
        break;
    default:
        n = "[Unknown thing]";
        break;
    }
    return n;
}

/* Overflow error. */

static void overflow(struct thing *jar, struct thing *thing)
{
    fprintf(stderr, "Attempt to add %s to %s: container is full.\n",
            nameof(thing), nameof(jar));
    exit(1);
}

/* Add something to a container. */

void Blink_add_to_container(Blink_CH thing, Blink_CH jar)
{
    struct row     *row;
    struct grid    *grid;
    struct overlay *overlay;

    if (!jar) {
        /* Item complete, send to display thread. */

        g_idle_add(New_thing_call, thing);     /* Pass it to the UI. */
        return;
    }

    switch (jar->type) {
    case Register:
        fprintf(stderr, "Register %s is not a container.\n", nameof(jar));
        exit(1);
        break;
    case Row:
        row = &jar->u.row;
        if (row->count >= MAX_ITEMS)
            overflow(jar, thing);        // No return.
        row->items[row->count++] = thing;
        break;
    case Grid:
        grid = &jar->u.grid;
        if (grid->count >= MAX_ITEMS)
            overflow(jar, thing);        // No return.
        grid->items[grid->count++] = thing;
        break;
    case Overlay:
        overlay = &jar->u.overlay;
        if (overlay->count >= MAX_ITEMS)
            overflow(jar, thing);        // No return.
        overlay->items[overlay->count++] = (GtkWidget *)thing;
        break;
    default:
        fprintf(stderr, "Unknown thing of type %d passed as container.\n",
                jar->type);
        exit(1);
        break;
    }
}

static struct thing *new_register(const char *name, Sim_RH handle,
                                  unsigned int width, unsigned int options)
{
    struct thing *this, *old;
    struct reg   *reg, *head;
    unsigned int  button_count, type;
    gboolean      is_fp;

    /* Check options for consistency. */

    if (options & RO_STYLE_MASK) {
        options &= ~(RO_SENSITIVITY | RO_ALT_COLOURS);
        button_count = 0;
    } else {
        button_count = width;
    }

    /* Create structure. */

    this = malloc(REGISTER_BASE_SIZE +
                      ((sizeof reg->u.b.buttons[0]) * button_count));
    if (!this)
        return NULL;
    this->type = Register;
    reg = &this->u.reg;
    reg->width = width;
    reg->options = options;
    type = (options & RO_STYLE_MASK);
    is_fp = (type == RO_STYLE_FP || type == RO_STYLE_FP_SPIN);
    if (is_fp) {
        reg->fp_value = 0.0;
    } else {
        reg->u_value = 0;
        reg->u_flags = 0;
    }
    reg->state = Valid;
    reg->clones = reg;          /* Circular list. */
    if (name)
        reg->name = strdup(name);
    else
        reg->name = NULL;
    reg->handle = handle;

    /* Hook the reg structure to the hash table. */

    old = g_hash_table_lookup(GHt, (gpointer)handle);
    if (old) {
        unsigned int head_type;
        gboolean     head_is_fp;

        if (old->type != Register) {
            fprintf(stderr,
                    "Register %s: handle previously used for item "
                    "%s of type %d.\n",
                    name ? name : "", nameof(old), (int)old->type);
            exit(1);
        }
        head = &old->u.reg;
        head_type = (head->options & RO_STYLE_MASK);
        head_is_fp =
            (head_type == RO_STYLE_FP || head_type == RO_STYLE_FP_SPIN);
        if (head_is_fp != is_fp) {
            fprintf(stderr,
                    "Floating-pont/integer conflict between registers "
                    "%s and %s.\n",
                    head->name, name);
            exit(1);
        }

        /* Chain it onto original for this handle. */

        reg->clones = head->clones;
        head->clones = reg;
    } else {
        g_hash_table_insert(GHt, (gpointer)handle, this);
    }
    return this;
}

/* Add a register.*/

void Blink_add_register(const char *name, Sim_RH handle, unsigned int width,
                        unsigned int options, Blink_CH container)
{
    struct thing *thing;

    thing = new_register(name, handle, width, options);
    if (!thing)
        return;
    if (container)
        Blink_add_to_container(thing, container);
    else
        g_idle_add(New_thing_call, thing);     /* Pass it to the UI. */
}

/* Start a new row. */

Blink_CH Blink_new_row(const char *name)
{
    struct thing *thing;
    struct row   *row;

    thing = (struct thing *)malloc(ROW_BASE_SIZE);
    if (!thing)
        return NULL;
    thing->type = Row;
    row = &thing->u.row;
    row->name = name ? strdup(name) : NULL;
    row->count = 0;
    return thing;
}

/* Start an overlay. Register and rows created while the overlay
 * is open share the same screen space.
 */

Blink_CH Blink_new_overlay(const char *name)
{
    struct thing   *thing;
    struct overlay *this;

    thing = (struct thing *)malloc(OVERLAY_BASE_SIZE);
    if (!thing) {
        fprintf(stderr, "No memory for overlayed register display %s\n", name);
        exit(1);
    }
    thing->type = Overlay;

    this = &thing->u.overlay;
    this->name = name ? strdup(name) : NULL;
    this->choice = 0;
    this->count = 0;
    this->stack = 0;
    return thing;
}

/* Start a new grid. */

Blink_CH Blink_new_grid(const char *name, int columns)
{
    struct thing *thing;
    struct grid   *grid;

    thing = (struct thing *)malloc(GRID_BASE_SIZE);
    if (!thing)
        return NULL;
    thing->type = Grid;
    grid = &thing->u.grid;
    grid->name = name ? strdup(name) : NULL;
    grid->columns = columns;
    grid->count = 0;
    return thing;
}

/* When the window is closed, this function is called in the simulation thread.
 */

static void do_exit(void)
{
    if (Sfp->sim_done)
        Sfp->sim_done();
    exit(0);
}

/* There is a new value to choose what is visible in an overlay. */

void Blink_change_overlay(Blink_CH thing, int value)
{
    struct overlay *this;

    if (thing->type != Overlay) {
        fprintf(stderr, "Item %s is not an overlay.\n", nameof(thing));
        exit(1);
    }
    this = &thing->u.overlay;
    if (this->choice == value || value < 0 || value >= this->count)
        return;
    this->choice = value;
    g_idle_add(Overlay_switch, this);
}

static struct reg *reg_from_handle(Sim_RH handle)
{
    struct thing *thing;

    thing = (struct thing *)g_hash_table_lookup(GHt, (gpointer)handle);
    if (!thing) {
        fprintf(stderr, "Unknown register handle.\n");
        exit(1);
    }
    if (thing->type != Register) {
        fprintf(stderr, "Item %s is not a register.\n", nameof(thing));
        exit(1);
    }
    return &thing->u.reg;
}

enum kind {i_value, f_value, flags};

static void new_data(Sim_RH handle, enum kind what, void *vp)
{
    struct reg   *rp;
    unsigned int  type;
    gboolean      is_fp, bad, go;

    rp = reg_from_handle(handle);
    type = (rp->options & RO_STYLE_MASK);
    is_fp = (type == RO_STYLE_FP || type == RO_STYLE_FP_SPIN);
    bad = FALSE;
    go = TRUE;

    /* Update the field ... */

    g_mutex_lock(&Simulation_mutex);
    go = (rp->state != User);
    if (go) {
        
        /* ... if no user update pending. */

        switch (what) {
        case i_value:
            if (is_fp)
                bad = TRUE;
            else if (rp->u_value != *(unsigned int *)vp)
                rp->u_value = *(unsigned int *)vp;
            else
                go = FALSE;
            break;
        case f_value:
            if (!is_fp)
                bad = TRUE;
            else if (rp->fp_value != *(double *)vp)
                rp->fp_value = *(double *)vp;
            else
                go = FALSE;
            break;
        case flags:
            if (is_fp)
                bad = TRUE;
            else if (rp->u_flags != *(unsigned int *)vp)
                rp->u_flags = *(unsigned int *)vp;
            else
                go = FALSE;
            break;
        }
        if (!bad && go && rp->state != Simulation) {
            rp->state = Simulation;

            /* Beware deadlock. */

            g_mutex_unlock(&Simulation_mutex);
            g_idle_add_full(G_PRIORITY_LOW,
                            what == flags ? Flag_call : Simulation_call,
                            (gpointer)rp,
                            NULL);
            return;
        }
    }
    g_mutex_unlock(&Simulation_mutex);
    if (bad) {
        fprintf(stderr, "Ignored incorrect data (type %d) for register %s.\n",
                (int)what, rp->name);
    }
}

/* The simulator has produced a new register value. */

void Blink_new_value(Sim_RH handle, unsigned int value)
{
    new_data(handle, i_value, &value);
}

void Blink_new_FP(Sim_RH handle, double value)
{
    new_data(handle, f_value, &value);
}

/* The simulator has produced a new flag value. */

void Blink_new_flags(Sim_RH handle, unsigned int value)
{
    new_data(handle, flags, &value);
}

/* Pass a table of strings to be used in a GtkComboBoxText widget.
 * The selection is treated as an integer "register.
 */

void Blink_new_strings(Sim_RH handle, const char * const *table)
{
    struct reg  *rp;

    rp = reg_from_handle(handle);
    if ((rp->options & RO_STYLE_MASK) != RO_STYLE_COMBO) {
        fprintf(stderr, "Strings supplied for non-combo-box item %s\n",
                rp->name);
        return;
    }

    /* Update the field ... */

    g_mutex_lock(&Simulation_mutex);
    rp->u.e.strings = table;
    g_mutex_unlock(&Simulation_mutex);
    g_idle_add_full(G_PRIORITY_LOW, New_strings_call, (gpointer)rp, NULL);
}

/* Store and retrieve a Blink handle. */

void Blink_store_handle(Blink_CH handle, Sim_RH key)
{
    struct thing *thing;

    /* Check for existing use. */

    thing = (struct thing *)g_hash_table_lookup(GHt, (gpointer)key);
    if (thing) {
        fprintf(stderr, "Key already used.\n");
        exit(1);
    }
    g_hash_table_insert(GHt, (gpointer)key, (gpointer)handle);
}

extern Blink_CH Blink_retrieve_handle(Sim_RH key)
{
    return (Blink_CH)g_hash_table_lookup(GHt, (gpointer)key);
}

/* This function tells the panel that the simulator has taken control of
 * execution.  At present it simply selects the value presented as
 * "Burst".
 */

extern void Blink_sim_ctl(unsigned int ctl)
{
    if (ctl != The_clock.sim_ctl) {
        The_clock.sim_ctl = ctl;
        g_idle_add_full(G_PRIORITY_LOW, Display_burst, NULL, NULL);
    }
}

/* Push new values into the simulation. */

static int push_changed_regs(void)
{
    struct reg *rp;
    int         rv = 0;

    g_mutex_lock(&Simulation_mutex);
    for (;;) {
        if (User_modified_regs == NULL) {
            g_mutex_unlock(&Simulation_mutex);
            return rv;
        }

        /* Special case indicates window closure. */

        if (User_modified_regs == EXIT_VALUE) {
            do_exit();
            return 0;
        }

        rp = User_modified_regs;
        User_modified_regs = rp->chain;
        if (rp->state == User) {
            unsigned int type, is_fp, v;
            double       fpv;

            v = is_fp = 0;                      // Silence gcc.
            fpv = 0.0;
            rp->state = Valid;

            if (rp->handle == COMBO_HANDLE) {
                v = rp->u_value;
            } else {
                type = (rp->options & RO_STYLE_MASK);
                is_fp = (type == RO_STYLE_FP || type == RO_STYLE_FP_SPIN);
                if (is_fp)
                    fpv = rp->fp_value;
                else
                    v = rp->u_value;
            }

            /* Call back with mutex unlocked. */

            g_mutex_unlock(&Simulation_mutex);
            if (rp->handle == COMBO_HANDLE) {
                /* Special case: combo-box changed. */

                if (Sfp->sim_push_unit && (*Sfp->sim_push_unit)(v))
                    rv = 1;
            } else if (is_fp) {
                if (Sfp->sim_push_fp(rp->handle, fpv))
                    rv = 1;
            } else {
                if (Sfp->sim_push_val(rp->handle, v))
                    rv = 1;
            }
            g_mutex_lock(&Simulation_mutex);
        }
    }
}

/* Wait for a tick or wakeup.  Argument is frequency in units of 0.1 Hz. */

static int snooze(int tick)
{
    gint64      wake_time;
    int         rv = 0;

    if (User_modified_regs) {
        rv = push_changed_regs();
        if (rv)
            return rv;
    }

    /* Sleep until new timing command or timeout.
     * The mutex is released while sleeping, recovered on wake.
     */

    wake_time = g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND) / tick;
    g_mutex_lock(&Simulation_mutex);
    g_cond_wait_until(&Simulation_waker, &Simulation_mutex, wake_time);
    g_mutex_unlock(&Simulation_mutex);
    if (User_modified_regs)
        rv = push_changed_regs();
    return rv;
}

/* Return information on how much to let simulation time advance. */

void Blink_run_control(struct run_control *rcp)
{
    static unsigned int cycles;         /* Current burst count. */
    static int          went;           /* Copy of cp->go. */
    static int          first;          /* No pause after button. */

 restart:
    while (!(cycles || The_clock.run || The_clock.go)) {
        /* Wait for command. */

        if (snooze(20)) {
            rcp->burst = 0;
            return;
        }
        first = 1;
        if (Sfp->sim_idle)
            (*Sfp->sim_idle)();
    }

    rcp->unit = The_clock.unit;
    rcp->rate = The_clock.rate;
    if (The_clock.fast) {
        /* Free-running - simply hand over parameters. */

        rcp->burst = The_clock.cycles_fast;

        /* Ensure controls are re-examined on next call. */

        The_clock.go = 0;
        cycles = 0;

        /* Check for user input. */

        if (User_modified_regs) {
            int         rv;

            rv = push_changed_regs();
            if (rv) {
                rcp->burst = 0;
                return;
            }
        }
    } else {
        /* Animation - single-step with pauses. */

        rcp->burst = The_clock.cycles_slow;
        if (cycles == 0) {
            went = The_clock.go;
            The_clock.go = 0;
            cycles = The_clock.cycles_slow;
        }

        /* Slow to human speed. */

        if (first) {
            first = 0;
        } else {
            if (snooze(rcp->rate)) {
                rcp->burst = 0;
                return;
            }
        }

        if (The_clock.go || The_clock.fast || !(went || The_clock.run) ||
            cycles-- > The_clock.cycles_slow) {
            /* Settings changed while sleeping. */

            cycles = 0;
            goto restart;
        }
        rcp->burst = 1;
    }
}

/* If the simulator needs to take control of execution, this function
 * may used to poll the panel for input.  It returns the same information
 * as Blink_run_control() but never blocks.
 */

extern void Blink_poll(struct run_control *rcp)
{
    if (User_modified_regs)
        (void)push_changed_regs();
    rcp->unit = The_clock.unit;
    rcp->rate = The_clock.rate;
    rcp->burst = The_clock.sim_ctl ? The_clock.cycles_sim :
                     The_clock.fast ? The_clock.cycles_fast :
                         The_clock.cycles_slow;
}
