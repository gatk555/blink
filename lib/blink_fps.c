#include "sim.h"

#define F(x) .x = Blink_##x,

struct blink_functs Blink_FPs = {
    F(init)
    F(run_control)
    F(stopped)
    F(new_row)
    F(new_grid)
    F(new_overlay)
    F(add_to_container)
    F(add_register)
    F(change_overlay)
    F(new_value)
    F(new_FP)
    F(new_flags)
    F(new_strings)
    F(store_handle)
    F(retrieve_handle)
    F(poll)
    F(sim_ctl)
};
    
