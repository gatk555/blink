This directory contains source code for a Verilog VPI module that
connects a Verilog simulation to a graphical UI "control Panel".
The Verilog code must contain calls to VPI-defined tasks that identify
registers and wires to be displayed and packs them into rows and
overlay containers.  Overlay containers are UI components that
contain multiple sets of other UI components, but only one is visible
at a time.  The visible set is controlled by the value of a Verilog
expression inside the simulation.

In addition to defining the UI, the Verilog code must also call the
$get_clock() task and use the returned value to control the advance of
simulation time.

Files:

vpi.c - source code for the VPI module.  The tasks are listed near the end.

panel.vh - simple Verilog macros that wrap some of the VPI tasks.

simple_clock.vh - Simple Verilog code that wraps the call to $get_clock().

test.v, test2.v - two very simple demonstration simulations.

Makefile - Build the software with Icarus Verilog.
