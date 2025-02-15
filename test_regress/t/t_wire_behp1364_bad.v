// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed into the Public Domain, for any use,
// without warranty, 2018 by Wilson Snyder.

module t (
   output o,
   output [1:0] oa,
   output reg ro,
   output reg [1:0] roa
   //1800 only:
   //output var vo;
   //output var [1:0] voa;
   );

   wire w;
   reg  r;

   initial begin
      w = '0;  // Error
      o = '0;  // Error
      oa = '0;  // Error
      r = '0;  // Not an error
      ro = '0;  // Not an error
      roa = '0;  // Not an error
      //vo = '0;  // Not an error
      //voa = '0;  // Not an error
   end

endmodule
