// Verilog include file -  VPI tasks and macros that invoke them
// to communicate with a UI "front panel".

// Declare a register or wire that will be displayed on the front panel.
// Arguments are the UI label and the verilog register or wire.
//
// Example:
//   $declare_register("display name", verilog_name);
//
// For simple cases, this macro shows a value with its internal name.

`define Visible(name) initial $declare_register("name", name);


// Variable argument function $declare_row() displays multiple registers
// in a single horizontal row.
//  $declare_row(row_name, name_1, v_name_1, ...)
//
// Example:
//   initial begin
//     $declare_row("Test_row", "A", R_A, "B", R_B, "Multi!", R_C);
//   end


// Declare an overlayed display area that shows one of a set of registers
// and register rows, with the active item chosen by the current value
// of an expression.  It may be defined directly if the expression is
// a "terminal", a register, wire, etc:
//
// Example:
//   initial $start_overlay("name", terminal);

// Now, following displayed registers and rows will be shown in the overlay,
// when that choice is active.
// Use $end_overlay to close the overlayed area to new entries.

// Using an expression to directly control an overlay does not work,
// unless expression evaluation is forced.  Use the Visible_overlay macro.
// The name is unquoted and must be a valid Verilog identifier.

`define Overlay_suffix Overlay_suffix_

`define Visible_overlay(name, expression) \
   wire [31:0] name`Overlay_suffix; \
   initial $start_overlay("name", name`Overlay_suffix); \
   assign name`Overlay_suffix = expression;

`define End_overlay initial $end_overlay();
