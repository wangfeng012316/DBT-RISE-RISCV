# DBT-RISE-RISCV
Core of an instruction set simulator based on DBT-RISE implementing the RISC-V ISA. The project is hosted at https://git.minres.com/DBT-RISE/DBT-RISE-RISCV .

This library provide the infrastructure to build RISC-V ISS. Currently part of the library are the following implementations adhering to version 2.2 of the 'The RISC-V Instruction Set Manual Volume I: User-Level ISA':

* RV32IMAC
* RV32GC
* RC64I
* RV64GC

All pass the respective compliance tests. Along with those ISA implementations there is a wrapper implementing the M/S/U modes inlcuding virtual memory management and CSRs as of privileged spec 1.10. The main.cpp in src allows to build a standalone ISS when integrated into a top-level project. For further information please have a look at [https://git.minres.com/VP/RISCV-VP](https://git.minres.com/VP/RISCV-VP).

Last but not least an SystemC wrapper is provided which allows easy integration into SystemC based virtual platforms.

Since DBT-RISE uses a generative approch other needed combinations or custom extension can be generated. For further information please contact [info@minres.com](mailto:info@minres.com).

