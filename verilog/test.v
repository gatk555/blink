`include "panel.vh"
`include "simple_clock.vh"

module Test(Clk);
   input wire Clk;
   reg [19:0] Bits;
   reg [15:0] Count;
   
   initial $declare_register("Data", Bits);

   `Visible(Count)

   reg R_A, R_B;
   reg [3:0] R_C;
   
   initial begin
      $declare_row("Test_row", "A", R_A, "B", R_B, "Multi", R_C);
   end


   `Visible_overlay(Status, R_A != 0)
   `Visible(R_A)
   `Visible(R_B)
   `End_overlay

   Driver D(Clk);

   initial begin
     Bits = 0;
     Count = 0;
   end

   always @(posedge Clk) begin
     if (Bits == 0)
       Bits = 1;
     else begin
       Bits = (Bits << 1) ^ (Bits & 359);
       if (Bits[19])
	  Count = Count + 1;
     end
     R_A = (Bits % 3) == 0;
     R_B = (Bits % 5) == 0;
     R_C = Bits % 13;
   end
endmodule
