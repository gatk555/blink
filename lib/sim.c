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
#include <glib.h>

#include "sim.h"
#include "no_gtk.h"     /* Really only GtkWidget etc. */
#include "panel.h"

/* Simulator-side interface to the Blink library. */

/* Static (for now) pointer to the simulator's functions. */

static struct simulator_calls *Sfp;

/* Hash table for Sim_RH handles to display structs. */

static GHashTable *GHt;

int Blink_init(const char * title, struct simulator_calls *calls)
{
    Sfp = calls;
    GHt = g_hash_table_new(NULL, NULL); /* Hash table gpointer->gpointer. */
    Start_Panel(title);
    return 1;
}

static struct reg *new_register(const char *name, Sim_RH handle,
                                unsigned int width, unsigned int options)
{
    struct reg *reg, *head;

    /* Check options for consistency. */

    if (options & RO_STYLE_MASK)
        options &= ~(RO_SENSITIVITY | RO_ALT_COLOURS);

    /* Create structure. */

    reg = malloc(sizeof *reg + ((sizeof reg->u.b.buttons[0]) * width));
    if (!reg)
        return NULL;
    reg->width = width;
    reg->value = 0;
    reg->options = options;
    reg->state = Valid;
    reg->clones = reg;          /* Circular list. */
    reg->name = strdup(name);
    reg->handle = handle;

    /* Hook the reg structure to the hash table. */

    head = g_hash_table_lookup(GHt, (gpointer)handle);
    if (head) {
        /* Chain it onto original for this handle. */

        reg->clones = head->clones;
        head->clones = reg;
    } else {
        g_hash_table_insert(GHt, (gpointer)handle, reg);
    }
    return reg;
}

/* Add a register.*/

void Blink_new_register(const char *name, Sim_RH handle, unsigned int width,
                        unsigned int options)
{
    struct reg *reg;

    reg = new_register(name, handle, width, options);
    if (!reg)
        return;
    g_idle_add(New_reg_call, reg);     /* Pass it to the UI. */
}

/* Start a new row. */

Blink_RH Blink_new_row(const char *name)
{
    struct row *row;

    row = malloc(sizeof *row + ((sizeof row->regs[0]) * MAX_ROW_ITEMS));
    if (!row)
        return row;
    row->name = strdup(name);
    row->count = 0;
    return row;
}

/* Add a register to a row. */

void Blink_add_register(const char *name, Sim_RH handle, unsigned int width,
                        unsigned int options, Blink_RH row)
{
    struct reg *reg;

    if (row->count >= MAX_ROW_ITEMS)
        return;
    reg = new_register(name, handle, width, options);
    if (!reg)
        return;
    row->regs[row->count++] = reg;
}

/* Finish-off a row. */

void Blink_close_row(Blink_RH row)
{
    size_t size;

    if (row->count == 0) {
        free(row);
        return;
    }
    if (row->count < MAX_ROW_ITEMS) {
        size = sizeof *row + ((sizeof row->regs[0]) * row->count);
        row = realloc(row, size);
        if (!row)
            return;
    }
    g_idle_add(New_row_call, row);     /* Pass it to the UI. */
}

/* Start/end an overlay. Register and rows created while the overlay
 * is open share the same screen space.
 */

void Blink_new_overlay(const char *name, Sim_OH handle)
{
    struct overlayed_regs *this;

    /* The overlay structure may already exist. */

    this = (struct overlayed_regs *)g_hash_table_lookup(GHt, (gpointer)handle);
    if (!this) {
        this =
            (struct overlayed_regs *)malloc(sizeof (struct overlayed_regs));
        if (!this) {
            fprintf(stderr, "No memory for overlayed register display %s\n",
                    name);
            exit(1);
        }
        this->name = strdup(name);
//        printf("New overlay: %s at %p\n", this->name, this);
        this->choice = 0;
        this->count = 0;
        this->stack = 0;

        /* Insert in hash table. */

        g_hash_table_insert(GHt, handle, (gpointer)this);
    }
    g_idle_add(Overlay_call, (gpointer)this);
}

void Blink_end_overlay(void)
{    
    g_idle_add(Overlay_call, NULL);
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

void Blink_change_overlay(Sim_OH handle, int value)
{
    struct overlayed_regs *this;

    this = (struct overlayed_regs *)g_hash_table_lookup(GHt, (gpointer)handle);
    if (!this)
        return;
    if (value < 0 || value >= this->count)
        return;
    this->choice = value;
    g_idle_add(Overlay_switch, this);
}

static void new_data(Sim_RH handle, int what, unsigned int value)
{
    struct reg  *rp;

    rp = (struct reg *)g_hash_table_lookup(GHt, (gpointer)handle);
    if (!rp)
        return;

    /* Update the field ... */

    g_mutex_lock(&Simulation_mutex);
    if (rp->state != User) {
        /* ... if no user update pending. */

        if (what)
            rp->flags = value;
        else
            rp->value = value;
        if (rp->state != Simulation) {
            rp->state = Simulation;

            /* Beware deadlock. */

            g_mutex_unlock(&Simulation_mutex);
            g_idle_add_full(G_PRIORITY_LOW,
                            what ? Flag_call : Simulation_call,
                            (gpointer)rp,
                            NULL);
            return;
        }
    }
    g_mutex_unlock(&Simulation_mutex);
}

/* The simulator has produced a new register value. */

void Blink_new_value(Sim_RH handle, unsigned int value)
{
    new_data(handle, 0, value);
}

/* The simulator has produced a new flag value. */

void Blink_new_flags(Sim_RH handle, unsigned int value)
{
    new_data(handle, 1, value);
}

/* Push new values into the simulation.  Mutex must be held. */

static int push_changed_regs(void)
{
    struct reg *rp;
    int         rv = 0;

    /* Special case indicates window closure. */

    if (User_modified_regs == EXIT_VALUE) {
        do_exit();
        return 0;
    }

    for (rp = User_modified_regs; rp; rp = rp->chain) {
        if (rp->state == User) {
            rp->state = Valid;
            if (Sfp->sim_push_val(rp->handle, rp->value))
                rv = 1;
        }
    }
    User_modified_regs = NULL;
    return rv;
}

/* Wait for a tick or wakeup.  Argument is frequency in units of 0.1 Hz. */

static int snooze(int tick)
{
    gint64      wake_time;
    int         rv = 0;

    wake_time = g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND) / tick;
    g_mutex_lock(&Simulation_mutex);
    if (User_modified_regs) {
        rv = push_changed_regs();
        if (rv) {
            g_mutex_unlock(&Simulation_mutex);
            return rv;
        }
    }

    /* Sleep until new timing command or timeout.
     * The mutex is released while sleeping, recovered on wake.
     */

    g_cond_wait_until(&Simulation_waker, &Simulation_mutex, wake_time);
    if (User_modified_regs)
        rv = push_changed_regs();
    g_mutex_unlock(&Simulation_mutex);
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
    }

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

            g_mutex_lock(&Simulation_mutex);
            rv = push_changed_regs();
            g_mutex_unlock(&Simulation_mutex);
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
