/* Basic clock signal generator, controlled by the UI. */

module Driver(Clk);
   output reg  Clk;
   integer     rate, burst;
   
   initial begin
      Clk = 0;
   end
   
      // This loop drives the clock, and so the simulation.
   always begin   
      Clk = #1 0; // Allow other module's initial actions.
      
      while (1) begin
	 $get_clock(rate, burst);
	 while (burst-- > 0) begin
	    // Do a clock half cycle.

	    Clk = Clk ^ 1;
	    #1;   // Make effects visible.
	 end
      end
   end
endmodule // Driver
