/*
 * Copyright 2021-2023 Giles Atkinson
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

#ifndef __SIM_H__
#define __SIM_H__

/* Simulator-side interface to the Blink library. */

/* Constants and typedefs. */

#define MAX_ITEMS 10
typedef struct thing *Blink_CH; /* Container handle. */
typedef void         *Sim_RH;   /* Simulator's register handle. */

/* Initialisation. */

struct simulator_calls {
    /* Blink calls these functions when the user has changed a register value.
     * The call is made from Blink_run_control() and if the function
     * returns a non-zero value, Blink_run_control() will return with a
     * cycle count of zero, without waiting for control input.
     */

    int  (*sim_push_val)(Sim_RH handle, unsigned int value);
    int  (*sim_push_fp)(Sim_RH handle, double value); // For RO_STYLE_FP.

    /* The "push units" function is called when the optional combo-box
     * in the top row changes value. Optional.
     */

    int  (*sim_push_unit)(unsigned int value);

    /* Called before blocking for user input in Blink_run_control(). */

    void (*sim_idle)(void);

    /* Called when the window has been closed. Program exits on return.
     * This is optional.
     */

    void (*sim_done)(void);
};

/* Blink_init returns 1 on success, otherwise 0. Arguments are window title
 * (may be NULL), callbacks structure and a null-terminated array of string
 * pointers to set labels in the optional units combobox in the clock row
 * (may be NULL), with an initial index value
 */

extern int Blink_init(const char                    *title,
                      const struct simulator_calls  *callbacks,
                      const char                   **unit_strings,
                      unsigned int                   initial_unit);

/* Simulator clock/time control.  This returns a cycle count and the
 * user-selected simulation speed.  The cycle count is the number of cycles
 * (whatever that means) that simulation should advance before calling
 * Blink_run_control() again.  The unit value may be used to select the
 * type of simulation cycle that is counted, or for anything else.
 * It is set by an optional combobox in the clock row.
 * The speed is likely useless.
 */

struct run_control {
    unsigned int        burst, unit, rate;
};

extern void Blink_run_control(struct run_control *rcp);
extern void Blink_stopped(void);        // Show simulator stopped. */

/* If the simulator needs to take control of execution, this function
 * may used to poll the panel for input.  It returns the same information
 * as Blink_run_control() but never blocks.
 */

extern void Blink_poll(struct run_control *rcp);

/* Blink set-up functions. */

/* Create a horizontal row that can contain several items, or an
 * overlay, which contains several items with only one visible at a time.
 * Such containers may be added to other containers.  Finish off by
 * adding the outermost container to NULL.  That makes the items visible
 * and no more of them can be added.
 */

extern Blink_CH Blink_new_row(const char *name);
extern Blink_CH Blink_new_overlay(const char *name);
extern void Blink_add_to_container(Blink_CH item, Blink_CH container);

/* The grid is another type of container: a two-dimensional table with
 * a fixed number of columns.  If the "columns" argument is negative
 * the columns may have varying widths.
 */

extern Blink_CH Blink_new_grid(const char *name, int columns);

/* Create a register and put it in a container, which may be NULL.
 * Width is the number of bits (and buttons for the default option),
 * number of significant figures for FP.
 * Registers may not be modified (value/flags) while their container is open.
 */

extern void Blink_add_register(const char *name, Sim_RH handle,
                               unsigned int width, unsigned int options,
                               Blink_CH container_handle);

/* Backward compatability. */

#define Blink_new_register(name, handle, width, options) \
    Blink_add_register(name, handle, width, options, NULL)

/* End of set-up, these calls change the display as simulation proceeds. */

extern void Blink_change_overlay(Blink_CH handle, int value);
extern void Blink_new_value(Sim_RH handle, unsigned int value);
extern void Blink_new_FP(Sim_RH handle, double value); // For RO_STYLE_FP.
extern void Blink_new_flags(Sim_RH handle, unsigned int flags);
extern void Blink_new_strings(Sim_RH handle, const char * const *table);

/* If a client has no means to store Blink's handles it can translate
 * its own.  Used by Verilog VPI for overlays.
 */

extern void     Blink_store_handle(Blink_CH handle, Sim_RH key);
extern Blink_CH Blink_retrieve_handle(Sim_RH key);

/* This function tells the panel that the simulator has taken control of
 * execution.  At present it simply selects the value presented as
 * "Burst".
 */

extern void Blink_sim_ctl(unsigned int);

/* Values for register options. */

#define RO_INSENSITIVE 0x10     /* Whole register starts insensitive. */
#define RO_SENSITIVITY 0x20     /* Flags select sensitivity. */
#define RO_ALT_COLOURS 0x40     /* Flags select alternate colour. */

/* The display style is a 3-bit field. */

#define RO_STYLE_MASK     7
#define RO_STYLE_BITS     0     /* Default style, individual bits. */
#define RO_STYLE_DECIMAL  1     /* Decimal number in GtkEntry. */
#define RO_STYLE_HEX      2     /* Hexadecimal number in GtkEntry. */
#define RO_STYLE_SPIN     3     /* Use GtkSpinButton (decimal). */
#define RO_STYLE_COMBO    4     /* Use GtkComboBoxText. */
#define RO_STYLE_FP       5     /* FP number (double) in GtkEntry. */
#define RO_STYLE_FP_SPIN  6     /* FP number (double) in GtkSpinButton. */


/* To make the shared library dlopen-friendly, an instance of this structure
 * is provided: struct blink_functs Blink_FPs.
 */

struct blink_functs {
    int      (*init)(const char *,
                     const struct simulator_calls *,
                     const char **,
                     unsigned int);
    void     (*run_control)(struct run_control *);
    void     (*stopped)(void);
    Blink_CH (*new_row)(const char *);
    Blink_CH (*new_grid)(const char *, int);
    Blink_CH (*new_overlay)(const char *);
    void     (*add_to_container)(Blink_CH, Blink_CH);
    void     (*add_register)(const char *, Sim_RH, unsigned int, unsigned int,
                             Blink_CH);
    void     (*change_overlay)(Blink_CH, int);
    void     (*new_value)(Sim_RH, unsigned int);
    void     (*new_FP)(Sim_RH handle, double value);
    void     (*new_flags)(Sim_RH, unsigned int);
    void     (*new_strings)(Sim_RH handle, const char * const *);
    void     (*store_handle)(Blink_CH handle, Sim_RH key);
    Blink_CH (*retrieve_handle)(Sim_RH key);
    void     (*poll)(struct run_control *rcp);
    void     (*sim_ctl)(unsigned int);
};
#endif /* __SIM_H__ */
