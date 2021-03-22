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

#ifndef __SIM_H__
#define __SIM_H__

/* Simulator-side interface to the Blink library. */

/* Constants and typedefs. */

#define MAX_ROW_ITEMS 10
typedef struct row *Blink_RH;   /* Row handle. */
typedef void       *Sim_RH;     /* Simulator's register handle. */
typedef void       *Sim_OH;     /* Simulator's overlay handle. */


/* Initialisation. */

struct simulator_calls {
    /* Blink calls this function when the user has changed a register value.
     * It is called with a mutex locked, so the function should be fast.
     * The call is made form Blink_run_control() and if the function
     * returns a non-zero value, Blink_run_control() will return with a
     * cycle count of zero, without waiting for control input.
     */

    int  (*sim_push_val)(Sim_RH handle, unsigned int value);

    /* Called when the window has been closed. Program exits on return.
     * This is optional.
     */

    void (*sim_done)(void);
};

/* Blink_init returns 1 on success, otherwise 0. */

extern int Blink_init(const char * title, struct simulator_calls *);

/* Simulator clock/time control.  This returns a cycle count and the
 * user-selected simulation speed.  The cycle count is the number of cycles
 * (whatever that means) that simulation should advance before calling
 *  Blink_run_control() again.  The speed is likely useless.
 */

struct run_control {
    unsigned int        burst, rate;
};

extern void Blink_run_control(struct run_control *rcp);
extern void Blink_stopped(void);        // Show simulator stopped. */

/* Blink display set-up. */

extern void Blink_new_register(const char *name, Sim_RH handle,
                               unsigned int width, unsigned int options);

/* Put several items in a horizontal row.  They may not be modified
 * (value/flags) while the row is open.
 */

extern Blink_RH Blink_new_row(const char *name);
extern void Blink_add_register(const char *name, Sim_RH handle,
                               unsigned int width, unsigned int options,
                               Blink_RH row_handle);
extern void Blink_close_row(Blink_RH row);

/* Put several items into the same display area, with one shown at a time. */

extern void Blink_new_overlay(const char *name, Sim_OH handle);
extern void Blink_end_overlay(void);

/* End of set-up, these calls change the display as simulation proceeds. */

extern void Blink_change_overlay(Sim_OH handle, int value);
extern void Blink_new_value(Sim_RH handle, unsigned int value);
extern void Blink_new_flags(Sim_RH handle, unsigned int flags);

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


/* To make the shared library dlopen-friendly, an instance of this structure
 * is provided: struct blink_functs Blink_FPs.
 */

struct blink_functs {
    int      (*init)(const char *, struct simulator_calls *);
    void     (*run_control)(struct run_control *);
    void     (*stopped)(void);
    void     (*new_register)(const char *, Sim_RH, unsigned int, unsigned int);
    Blink_RH (*new_row)(const char *);
    void     (*add_register)(const char *, Sim_RH, unsigned int, unsigned int,
                             Blink_RH);
    void     (*close_row)(Blink_RH);
    void     (*new_overlay)(const char *, Sim_OH);
    void     (*end_overlay)(void);
    void     (*change_overlay)(Sim_OH, int);
    void     (*new_value)(Sim_RH, unsigned int);
    void     (*new_flags)(Sim_RH, unsigned int);
};
#endif /* __SIM_H__ */
