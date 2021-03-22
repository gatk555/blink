#include "sim.h"

#define F(x) .x = Blink_##x,

struct blink_functs Blink_FPs = {
    F(init)
    F(run_control)
    F(stopped)
    F(new_register)
    F(new_row)
    F(add_register)
    F(close_row)
    F(new_overlay)
    F(end_overlay)
    F(change_overlay)
    F(new_value)
    F(new_flags)
};
    
