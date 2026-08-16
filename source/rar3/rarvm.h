/* Copyright 2015 the unarr project authors (see AUTHORS file).
   License: LGPLv3 */

/* adapted from https://code.google.com/p/theunarchiver/source/browse/XADMaster/RARVirtualMachine.h */

/* HaulNX note: trimmed from unarr's original rarvm.h/.c. The original pair
 * implements RAR3's *general* filter bytecode interpreter (program building
 * in RARProgram/RARCreateProgram/RARProgramAddInstr/... plus a ~40-opcode
 * RARExecuteProgram switch statement) so it can run any filter program an
 * archive supplies. HaulNX deliberately doesn't ship that interpreter (see
 * filter-rar.c) -- real-world RAR encoders only ever emit five known
 * "standard" filter programs, which filter-rar.c recognizes by fingerprint
 * and runs as small native functions instead. What survives here is only
 * the plain data (the VM's register/memory scratch space) and the masked
 * memory accessors those native functions call directly -- not an
 * instruction interpreter. An archive with a genuinely custom filter
 * program still isn't supported, same as it wasn't before this file was
 * trimmed; see UPSTREAM_COMMIT.txt for the untrimmed original. */

#ifndef rar_vm_h
#define rar_vm_h

#include <stdint.h>
#include <stdbool.h>

#define RARProgramMemorySize 0x40000
#define RARProgramMemoryMask (RARProgramMemorySize - 1)
#define RARProgramWorkSize 0x3c000
#define RARProgramGlobalSize 0x2000
#define RARProgramSystemGlobalAddress RARProgramWorkSize
#define RARProgramSystemGlobalSize 64
#define RARProgramUserGlobalAddress (RARProgramSystemGlobalAddress + RARProgramSystemGlobalSize)
#define RARProgramUserGlobalSize (RARProgramGlobalSize - RARProgramSystemGlobalSize)

typedef struct RARVirtualMachine RARVirtualMachine;

struct RARVirtualMachine {
    uint32_t registers[8];
    uint8_t memory[RARProgramMemorySize + sizeof(uint32_t) /* overflow sentinel */];
};

/* Memory access (used directly by the native standard-filter functions in
 * filter-rar.c, not just by an instruction interpreter -- these are plain
 * bounds-masked reads/writes, kept as-is from upstream). */

uint32_t RARVirtualMachineRead32(RARVirtualMachine *vm, uint32_t address);
void RARVirtualMachineWrite32(RARVirtualMachine *vm, uint32_t address, uint32_t val);
uint8_t RARVirtualMachineRead8(RARVirtualMachine *vm, uint32_t address);
void RARVirtualMachineWrite8(RARVirtualMachine *vm, uint32_t address, uint8_t val);

#endif
