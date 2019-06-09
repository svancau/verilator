#include "Vpwm.h"
#include "verilated.h"
#include "verilated_fst_c.h"

int main(int argc, char** argv, char** env) {
  vluint64_t main_time = 0;
  Verilated::commandArgs(argc, argv);
  Vpwm *top = new Vpwm;
  VerilatedFstC* tfp = new VerilatedFstC;
  Verilated::traceEverOn(true);
  top->trace(tfp, 99);  // Trace 99 levels of hierarchy
  tfp->open("obj_dir/pwm.fst");
  top->CLK = 0;
  top->DUTY = 0;
  for(long cycles = 0; cycles < 10000000; cycles++) {
    top->CLK = 0;
    if (cycles % 65536 == 0)
        top->DUTY += 10;
    top->eval();
    main_time += 5;
    tfp->dump(main_time);

    top->CLK = 1;
    top->eval();
    main_time += 5;
    tfp->dump(main_time);
    if (Verilated::gotFinish())
        break;
  }
  delete top;
  exit(0);
}
