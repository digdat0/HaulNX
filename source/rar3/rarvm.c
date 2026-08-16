/* Copyright 2015 the unarr project authors (see AUTHORS file).
   License: LGPLv3 */

/* adapted from https://code.google.com/p/theunarchiver/source/browse/XADMaster/RARVirtualMachine.c */

/* HaulNX note: see rarvm.h -- the program-building API and RARExecuteProgram
 * (the general bytecode interpreter) are removed. Only the masked
 * memory-access helpers remain, verbatim from upstream. */

#include "rarvm.h"

static uint32_t _RARRead32(const uint8_t *b)
{
    return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[0];
}

static void _RARWrite32(uint8_t *b, uint32_t n)
{
    b[3] = (n >> 24) & 0xFF;
    b[2] = (n >> 16) & 0xFF;
    b[1] = (n >> 8) & 0xFF;
    b[0] = n & 0xFF;
}

uint32_t RARVirtualMachineRead32(RARVirtualMachine *vm, uint32_t address)
{
    return _RARRead32(&vm->memory[address & RARProgramMemoryMask]);
}

void RARVirtualMachineWrite32(RARVirtualMachine *vm, uint32_t address, uint32_t val)
{
    _RARWrite32(&vm->memory[address & RARProgramMemoryMask], val);
}

uint8_t RARVirtualMachineRead8(RARVirtualMachine *vm, uint32_t address)
{
    return vm->memory[address & RARProgramMemoryMask];
}

void RARVirtualMachineWrite8(RARVirtualMachine *vm, uint32_t address, uint8_t val)
{
    vm->memory[address & RARProgramMemoryMask] = val;
}
