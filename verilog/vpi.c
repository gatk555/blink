/*
 * Copyright (c) 2002 Stephen Williams (steve@icarus.com)
 * Copyright (c) 2023 Giles Atkinson
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

#include <vpi_user.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "sim.h"

/* TODO - release iterators vpi_free_object() page 720 really 350. */

static int Pushing;     // Echo control.

/* Current overlay handle: FIX ME. */

static Blink_CH Current_overlay;

/* Functions that VPI calls have a returned parameter argument that
 * is not used here.
 */

#define UNUSED __attribute__((unused))

/* Get the initial handle passed to vpi_scan() to
 * get handles to successive arguments to a VPI call.
 */

static vpiHandle get_args_handle(void)
{
    vpiHandle   callh;

    callh = vpi_handle(vpiSysTfCall, 0);
    return vpi_iterate(vpiArgument, callh);
}

/* Get the next argument value for a VPI call.
 * The second argument to this function holds context, and
 * must be NULL when requesting the first VPI argument.
 */

static void get_arg_val(s_vpi_value *vp, vpiHandle *hp)
{
    vpiHandle   valh;

    if (!*hp) {
        /* The initial argument of a VPI call. */

        *hp = get_args_handle();
    }
    valh = vpi_scan(*hp);
    if (valh)
        vpi_get_value(valh, vp);
    else
        vp->format = vpiSuppressVal; // Use as sentinel.
}

/* Set the next argument value for a VPI call.
 * Second argument as for get_arg_val().
 */

static void set_arg_val(s_vpi_value *vp, vpiHandle *hp)
{
    vpiHandle   valh;

    if (!*hp) {
        /* The initial argument of a VPI call. */

        *hp = get_args_handle();
    }
    valh = vpi_scan(*hp);
    vpi_put_value(valh, vp, NULL, vpiNoDelay);
}

/* Function called by Blink with new value. */

static int push_val(void *handle, unsigned int value)
{
    struct __vpiHandle *h = (struct __vpiHandle *)handle;
    s_vpi_value         val;

    Pushing = 1;
    val.format = vpiIntVal;
    val.value.integer = value;
    vpi_put_value(h, &val, NULL, vpiNoDelay);
    Pushing = 0;
    return 0;
}

/* VPI function to control the clock.  This is called from Verilog code,
 * and returns the rate and number of time steps the simulation should advance.
 */

static PLI_INT32 get_clock(char *user_data UNUSED)
{
    s_vpi_value        val;
    vpiHandle          argv = 0;
    struct run_control run_control;

    Blink_run_control(&run_control);

    /* Return rate and burst to the VPI caller. */

    val.format = vpiIntVal;
    val.value.integer = run_control.rate;
    set_arg_val(&val, &argv);
    val.value.integer = run_control.burst;
    set_arg_val(&val, &argv);
    return 0;
}

/* Value-change callback function. */

PLI_INT32 vc_cb(struct t_cb_data *cb)
{
    if (!Pushing)       // Do not reflect back values set bu user.
        Blink_new_value(cb->obj, cb->value->value.integer);
    return 0;
}

/* An overlay's controlling expression changed value. */

PLI_INT32 ov_cb(struct t_cb_data *cb)
{
    Blink_CH    blink_handle;
    s_vpi_value val;

    /* Get the matching Blink handle. */

    blink_handle = Blink_retrieve_handle(cb->obj);
    if (!blink_handle)
        return 0;

    /* ... and the value. */

    val.format = vpiIntVal;
    vpi_get_value(cb->obj, &val);
    Blink_change_overlay(blink_handle, val.value.integer);
    return 0;
}

/* Set a value-change callback on something. */

static vpiHandle set_watch(vpiHandle handle,
                           PLI_INT32 (*fn)(struct t_cb_data *))
{
    static s_vpi_time        s_time = {.type = vpiSuppressTime};
    static s_vpi_value       s_value = {.format = vpiIntVal};
    static struct t_cb_data  cb = {
                                 .reason = cbValueChange,
                                 .time = &s_time, .value = &s_value
                             };

    cb.obj = handle;
    cb.cb_rtn = fn;
    return vpi_register_cb(&cb);
}

/* VPI function to record details of a register.
 * The arguments are a handle to a Verilog
 * argument list and an optional Blink row handle.
 *
 * Returns 1 on success, 0 if argument list exhausted.
 */

static int record_register(vpiHandle argv, Blink_CH row_handle)
{
    s_vpi_value              val;
    const char              *name;
    vpiHandle                reg;
    int                      width;

    /* Get the name. */

    val.format = vpiStringVal;
    get_arg_val(&val, &argv);
    if (val.format != vpiStringVal || !val.value.str || !val.value.str[0])
        return 0;
    name = val.value.str;

    /* Get handle and width. */

    reg = vpi_scan(argv);               /* Second VPI argument. */
    if (!reg)
        return 0;
    width = vpi_get(vpiSize, reg);
    Blink_add_register(name, reg, width, 0, row_handle);

    /* Callback here, vpi_register_cb() with vpiSuppressTime. */

    if (!set_watch(reg, vc_cb))
        vpi_printf("Failed to add callback for register %s\n", name);

    return 1;
}

/* VPI function to declare a register.
 * This is called from Verilog code.
 */

static PLI_INT32 declare_register(char *user_data UNUSED)
{
    vpiHandle   argv;

    argv = get_args_handle();
    record_register(argv, Current_overlay);
    return 0;
}

/* VPI function to declare a row of small registers.
 * This is called from Verilog code.
 */

static PLI_INT32 declare_row(char *user_data UNUSED)
{
    s_vpi_value val;
    vpiHandle   argv = 0;
    Blink_CH    row_handle;
    int         count;

    /* Get the row name. */

    val.format = vpiStringVal;
    get_arg_val(&val, &argv);
    if (!val.value.str) {
        vpi_printf("No row name in declare_row()!\n");
        return 0;
    }
    row_handle = Blink_new_row(val.value.str);
    if (!row_handle)
        return 0;

    /* Get the contents. */

    for (count = 0; count < MAX_ITEMS; count++) {
        if (!record_register(argv, row_handle))
            break;
    }
    if (count == 0)
        return 0;       /* No items. */

    /* Pass to UI thread. */

    Blink_add_to_container(row_handle, Current_overlay);
    return 0;
}

/* Start an overlayed register display.
 * This is called from Verilog code.
 */

static PLI_INT32 start_overlay(char *user_data UNUSED)
{
    s_vpi_value val;
    vpiHandle   argv = 0;
    vpiHandle   expr;

    /* Get the overlay name. */

    val.format = vpiStringVal;
    get_arg_val(&val, &argv);
    if (!val.value.str) {
        vpi_printf("No name in start_overlay()!\n");
        return 0;
    }

    /* Second argument is the controlling expression. */

    expr = vpi_scan(argv);               /* Second VPI argument. */
    if (!expr) {
        vpi_printf("No controlling expression for overlay %s\n",
                   val.value.str);
        return 0;
    }
    Current_overlay = Blink_new_overlay(val.value.str);
    Blink_store_handle(Current_overlay, expr);

    /* Set watch on it. */

    set_watch(expr, ov_cb);
    return 0;
}

/* End any overlayed register display.
 * This may be called from Verilog code.
 */

static PLI_INT32 end_overlay(char *user_data UNUSED)
{
    Blink_add_to_container(Current_overlay, NULL);
    Current_overlay = NULL;
    return 0;
}

/* The discriminating expression for an overlayed
 * register display has changed.  Update the
 * visible item.  This may be called from Verilog code.
 */

static PLI_INT32 new_overlayed_item(char *user_data UNUSED)
{
    vpiHandle    handle;
    vpiHandle   argv = 0;
    Blink_CH    blink_handle;
    s_vpi_value val;

    /* Get the register. */

    argv = get_args_handle();
    handle = vpi_scan(argv);
    blink_handle = Blink_retrieve_handle(handle);

    /* ... and the value. */

    val.format = vpiIntVal;
    get_arg_val(&val, &argv);
    Blink_change_overlay(blink_handle, val.value.integer);
    return 0;
}

/* Blink library initialisation. */

static struct simulator_calls blink_functions = {
    .sim_push_val = push_val
};

static PLI_INT32 start_cb(struct t_cb_data *cb)
{
    if (!Blink_init(cb->user_data, &blink_functions, NULL, 0))
        exit(1);
    return 0;
}

/* Verilog initialisation stuff. The Verilog system calls do_register after
 * loading this dynamic library.  Register entry points.
 */

static s_vpi_systf_data stuff[] = {
    {vpiSysTask, 0, "$declare_register", declare_register, NULL, 0, NULL},
    {vpiSysTask, 0, "$declare_row", declare_row, NULL, 0, NULL},
    {vpiSysTask, 0, "$get_clock", get_clock, NULL, 0, NULL},
    {vpiSysTask, 0, "$start_overlay", start_overlay, NULL, 0, NULL},
    {vpiSysTask, 0, "$end_overlay", end_overlay, NULL, 0, NULL},
    {vpiSysTask, 0, "$new_overlayed_item", new_overlayed_item, NULL, 0, NULL},
};
#define ENTRIES(A) (sizeof A / sizeof A[0])

static void do_register(void)
{
    struct t_vpi_vlog_info  info;
    static struct t_cb_data cbd = { .reason = cbEndOfCompile,
                                    .cb_rtn = start_cb };
    char                   *title;
    unsigned int            i;
    char                    tbuf[1024];

    /* Get the program name. */

    if (vpi_get_vlog_info(&info)) {
        snprintf(tbuf, sizeof tbuf, "%s", info.argv[0]);
        title = basename(tbuf);
#if 0
        for (i = 0; i < info.argc; ++i)
            vpi_printf("%d: %s\n", i, info.argv[i]);
        vpi_printf("P: %s V: %s\n", info.product, info.version);
#endif
    } else {
        vpi_printf("Failed to get invocation information.\n");
        title = "Verilog simulation";
    }

    /* Delay panal initialisation to the start of simulation,
     * as this code is also called when Verilog source is compiled.
     */

    cbd.user_data = strdup(title);
    vpi_register_cb(&cbd);

    for (i = 0; i < ENTRIES(stuff); ++i)
        vpi_register_systf(&stuff[i]);
}

/* This is a table of registration functions. This table is the external
 * symbol that the simulator looks for when loading this .vpi module.
 */

void (*vlog_startup_routines[])(void) = {
      do_register,
      0
};
