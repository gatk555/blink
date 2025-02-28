/* Very simple tester for Blinken operation, particularly doubly-displayed
 * data and assigned "wire" values.
 */

`include "panel.vh"
`include "simple_clock.vh"

 module Test(Clk);
   input wire Clk;
   reg [19:0] Bits;
   wire [19:0] Copy;
   

   initial $declare_register("Data", Bits);

   `Visible(Bits)
   `Visible(Copy)
   assign Copy = ~Bits;  // Simple assign does not work.  Icarus bug?

   Driver A(Clk);
endmodule // Test
