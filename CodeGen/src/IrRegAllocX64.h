// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/IrData.h"
#include "Luau/RegisterX64.h"

#include <array>
#include <initializer_list>

namespace Luau
{
namespace CodeGen
{

struct IrRegAllocX64
{
    IrRegAllocX64(IrFunction& function);

    RegisterX64 allocGprReg(SizeX64 preferredSize);
    RegisterX64 allocXmmReg();

    RegisterX64 allocGprRegOrReuse(SizeX64 preferredSize, uint32_t index, std::initializer_list<IrOp> oprefs);
    RegisterX64 allocXmmRegOrReuse(uint32_t index, std::initializer_list<IrOp> oprefs);

    void freeReg(RegisterX64 reg);
    void freeLastUseReg(IrInst& target, uint32_t index);
    void freeLastUseRegs(const IrInst& inst, uint32_t index);

    IrFunction& function;

    std::array<bool, 16> freeGprMap;
    std::array<bool, 16> freeXmmMap;
};

struct ScopedRegX64
{
    ScopedRegX64(IrRegAllocX64& owner, SizeX64 size);
    ScopedRegX64(IrRegAllocX64& owner, RegisterX64 reg);
    ~ScopedRegX64();

    ScopedRegX64(const ScopedRegX64&) = delete;
    ScopedRegX64& operator=(const ScopedRegX64&) = delete;

    void free();

    IrRegAllocX64& owner;
    RegisterX64 reg;
};

} // namespace CodeGen
} // namespace Luau
