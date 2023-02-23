// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/IrBuilder.h"
#include "Luau/IrAnalysis.h"
#include "Luau/IrDump.h"
#include "Luau/IrUtils.h"
#include "Luau/OptimizeFinalX64.h"

#include "doctest.h"

#include <limits.h>

using namespace Luau::CodeGen;

class IrBuilderFixture
{
public:
    void constantFold()
    {
        for (size_t i = 0; i < build.function.instructions.size(); i++)
        {
            IrInst& inst = build.function.instructions[i];

            applySubstitutions(build.function, inst);
            foldConstants(build, build.function, uint32_t(i));
        }
    }

    template<typename F>
    void withOneBlock(F&& f)
    {
        IrOp main = build.block(IrBlockKind::Internal);
        IrOp a = build.block(IrBlockKind::Internal);

        build.beginBlock(main);
        f(a);

        build.beginBlock(a);
        build.inst(IrCmd::LOP_RETURN, build.constUint(1));
    };

    template<typename F>
    void withTwoBlocks(F&& f)
    {
        IrOp main = build.block(IrBlockKind::Internal);
        IrOp a = build.block(IrBlockKind::Internal);
        IrOp b = build.block(IrBlockKind::Internal);

        build.beginBlock(main);
        f(a, b);

        build.beginBlock(a);
        build.inst(IrCmd::LOP_RETURN, build.constUint(1));

        build.beginBlock(b);
        build.inst(IrCmd::LOP_RETURN, build.constUint(2));
    };

    void checkEq(IrOp lhs, IrOp rhs)
    {
        CHECK_EQ(lhs.kind, rhs.kind);
        LUAU_ASSERT(lhs.kind != IrOpKind::Constant && "can't compare constants, each ref is unique");
        CHECK_EQ(lhs.index, rhs.index);
    }

    void checkEq(IrOp instOp, const IrInst& inst)
    {
        const IrInst& target = build.function.instOp(instOp);
        CHECK(target.cmd == inst.cmd);
        checkEq(target.a, inst.a);
        checkEq(target.b, inst.b);
        checkEq(target.c, inst.c);
        checkEq(target.d, inst.d);
        checkEq(target.e, inst.e);
    }

    IrBuilder build;

    // Luau.VM headers are not accessible
    static const int tnil = 0;
    static const int tboolean = 1;
    static const int tnumber = 3;
};

TEST_SUITE_BEGIN("Optimization");

TEST_CASE_FIXTURE(IrBuilderFixture, "FinalX64OptCheckTag")
{
    IrOp block = build.block(IrBlockKind::Internal);
    IrOp fallback = build.block(IrBlockKind::Fallback);

    build.beginBlock(block);
    IrOp tag1 = build.inst(IrCmd::LOAD_TAG, build.vmReg(2));
    build.inst(IrCmd::CHECK_TAG, tag1, build.constTag(0), fallback);
    IrOp tag2 = build.inst(IrCmd::LOAD_TAG, build.vmConst(5));
    build.inst(IrCmd::CHECK_TAG, tag2, build.constTag(0), fallback);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(fallback);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    optimizeMemoryOperandsX64(build.function);

    // Load from memory is 'inlined' into CHECK_TAG
    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   CHECK_TAG R2, tnil, bb_fallback_1
   CHECK_TAG K5, tnil, bb_fallback_1
   LOP_RETURN 0u

bb_fallback_1:
   LOP_RETURN 0u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "FinalX64OptBinaryArith")
{
    IrOp block = build.block(IrBlockKind::Internal);

    build.beginBlock(block);
    IrOp opA = build.inst(IrCmd::LOAD_DOUBLE, build.vmReg(1));
    IrOp opB = build.inst(IrCmd::LOAD_DOUBLE, build.vmReg(2));
    build.inst(IrCmd::ADD_NUM, opA, opB);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    optimizeMemoryOperandsX64(build.function);

    // Load from memory is 'inlined' into second argument
    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   %0 = LOAD_DOUBLE R1
   %2 = ADD_NUM %0, R2
   LOP_RETURN 0u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "FinalX64OptEqTag1")
{
    IrOp block = build.block(IrBlockKind::Internal);
    IrOp trueBlock = build.block(IrBlockKind::Internal);
    IrOp falseBlock = build.block(IrBlockKind::Internal);

    build.beginBlock(block);
    IrOp opA = build.inst(IrCmd::LOAD_TAG, build.vmReg(1));
    IrOp opB = build.inst(IrCmd::LOAD_TAG, build.vmReg(2));
    build.inst(IrCmd::JUMP_EQ_TAG, opA, opB, trueBlock, falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(trueBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    optimizeMemoryOperandsX64(build.function);

    // Load from memory is 'inlined' into first argument
    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   %1 = LOAD_TAG R2
   JUMP_EQ_TAG R1, %1, bb_1, bb_2

bb_1:
   LOP_RETURN 0u

bb_2:
   LOP_RETURN 0u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "FinalX64OptEqTag2")
{
    IrOp block = build.block(IrBlockKind::Internal);
    IrOp trueBlock = build.block(IrBlockKind::Internal);
    IrOp falseBlock = build.block(IrBlockKind::Internal);

    build.beginBlock(block);
    IrOp opA = build.inst(IrCmd::LOAD_TAG, build.vmReg(1));
    IrOp opB = build.inst(IrCmd::LOAD_TAG, build.vmReg(2));
    build.inst(IrCmd::STORE_TAG, build.vmReg(6), opA);
    build.inst(IrCmd::JUMP_EQ_TAG, opA, opB, trueBlock, falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(trueBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    optimizeMemoryOperandsX64(build.function);

    // Load from memory is 'inlined' into second argument is it can't be done for the first one
    // We also swap first and second argument to generate memory access on the LHS
    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   %0 = LOAD_TAG R1
   STORE_TAG R6, %0
   JUMP_EQ_TAG R2, %0, bb_1, bb_2

bb_1:
   LOP_RETURN 0u

bb_2:
   LOP_RETURN 0u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "FinalX64OptEqTag3")
{
    IrOp block = build.block(IrBlockKind::Internal);
    IrOp trueBlock = build.block(IrBlockKind::Internal);
    IrOp falseBlock = build.block(IrBlockKind::Internal);

    build.beginBlock(block);
    IrOp table = build.inst(IrCmd::LOAD_POINTER, build.vmReg(1));
    IrOp arrElem = build.inst(IrCmd::GET_ARR_ADDR, table, build.constInt(0));
    IrOp opA = build.inst(IrCmd::LOAD_TAG, arrElem);
    build.inst(IrCmd::JUMP_EQ_TAG, opA, build.constTag(0), trueBlock, falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(trueBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    optimizeMemoryOperandsX64(build.function);

    // Load from memory is 'inlined' into first argument
    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   %0 = LOAD_POINTER R1
   %1 = GET_ARR_ADDR %0, 0i
   %2 = LOAD_TAG %1
   JUMP_EQ_TAG %2, tnil, bb_1, bb_2

bb_1:
   LOP_RETURN 0u

bb_2:
   LOP_RETURN 0u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "FinalX64OptJumpCmpNum")
{
    IrOp block = build.block(IrBlockKind::Internal);
    IrOp trueBlock = build.block(IrBlockKind::Internal);
    IrOp falseBlock = build.block(IrBlockKind::Internal);

    build.beginBlock(block);
    IrOp opA = build.inst(IrCmd::LOAD_DOUBLE, build.vmReg(1));
    IrOp opB = build.inst(IrCmd::LOAD_DOUBLE, build.vmReg(2));
    build.inst(IrCmd::JUMP_CMP_NUM, opA, opB, trueBlock, falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(trueBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    build.beginBlock(falseBlock);
    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    optimizeMemoryOperandsX64(build.function);

    // Load from memory is 'inlined' into first argument
    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   %1 = LOAD_DOUBLE R2
   JUMP_CMP_NUM R1, %1, bb_1, bb_2

bb_1:
   LOP_RETURN 0u

bb_2:
   LOP_RETURN 0u

)");
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("ConstantFolding");

TEST_CASE_FIXTURE(IrBuilderFixture, "Numeric")
{
    IrOp block = build.block(IrBlockKind::Internal);

    build.beginBlock(block);
    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::ADD_INT, build.constInt(10), build.constInt(20)));
    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::ADD_INT, build.constInt(INT_MAX), build.constInt(1)));

    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::SUB_INT, build.constInt(10), build.constInt(20)));
    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::SUB_INT, build.constInt(INT_MIN), build.constInt(1)));

    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::ADD_NUM, build.constDouble(2), build.constDouble(5)));
    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::SUB_NUM, build.constDouble(2), build.constDouble(5)));
    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::MUL_NUM, build.constDouble(2), build.constDouble(5)));
    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::DIV_NUM, build.constDouble(2), build.constDouble(5)));
    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::MOD_NUM, build.constDouble(5), build.constDouble(2)));
    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::POW_NUM, build.constDouble(5), build.constDouble(2)));

    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::UNM_NUM, build.constDouble(5)));

    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NOT_ANY, build.constTag(tnil), build.inst(IrCmd::LOAD_DOUBLE, build.vmReg(1))));
    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NOT_ANY, build.constTag(tnumber), build.inst(IrCmd::LOAD_DOUBLE, build.vmReg(1))));
    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NOT_ANY, build.constTag(tboolean), build.constInt(0)));
    build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NOT_ANY, build.constTag(tboolean), build.constInt(1)));

    build.inst(IrCmd::STORE_DOUBLE, build.vmReg(0), build.inst(IrCmd::INT_TO_NUM, build.constInt(8)));

    build.inst(IrCmd::LOP_RETURN, build.constUint(0));

    updateUseCounts(build.function);
    constantFold();

    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   STORE_INT R0, 30i
   STORE_INT R0, -2147483648i
   STORE_INT R0, -10i
   STORE_INT R0, 2147483647i
   STORE_DOUBLE R0, 7
   STORE_DOUBLE R0, -3
   STORE_DOUBLE R0, 10
   STORE_DOUBLE R0, 0.40000000000000002
   STORE_DOUBLE R0, 1
   STORE_DOUBLE R0, 25
   STORE_DOUBLE R0, -5
   STORE_INT R0, 1i
   STORE_INT R0, 0i
   STORE_INT R0, 1i
   STORE_INT R0, 0i
   STORE_DOUBLE R0, 8
   LOP_RETURN 0u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "ControlFlowEq")
{
    withTwoBlocks([this](IrOp a, IrOp b) {
        build.inst(IrCmd::JUMP_EQ_TAG, build.constTag(tnil), build.constTag(tnil), a, b);
    });

    withTwoBlocks([this](IrOp a, IrOp b) {
        build.inst(IrCmd::JUMP_EQ_TAG, build.constTag(tnil), build.constTag(tnumber), a, b);
    });

    withTwoBlocks([this](IrOp a, IrOp b) {
        build.inst(IrCmd::JUMP_EQ_INT, build.constInt(0), build.constInt(0), a, b);
    });

    withTwoBlocks([this](IrOp a, IrOp b) {
        build.inst(IrCmd::JUMP_EQ_INT, build.constInt(0), build.constInt(1), a, b);
    });

    updateUseCounts(build.function);
    constantFold();

    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   JUMP bb_1

bb_1:
   LOP_RETURN 1u

bb_3:
   JUMP bb_5

bb_5:
   LOP_RETURN 2u

bb_6:
   JUMP bb_7

bb_7:
   LOP_RETURN 1u

bb_9:
   JUMP bb_11

bb_11:
   LOP_RETURN 2u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "NumToIndex")
{
    withOneBlock([this](IrOp a) {
        build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NUM_TO_INDEX, build.constDouble(4), a));
        build.inst(IrCmd::LOP_RETURN, build.constUint(0));
    });

    withOneBlock([this](IrOp a) {
        build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NUM_TO_INDEX, build.constDouble(1.2), a));
        build.inst(IrCmd::LOP_RETURN, build.constUint(0));
    });

    withOneBlock([this](IrOp a) {
        IrOp nan = build.inst(IrCmd::DIV_NUM, build.constDouble(0.0), build.constDouble(0.0));
        build.inst(IrCmd::STORE_INT, build.vmReg(0), build.inst(IrCmd::NUM_TO_INDEX, nan, a));
        build.inst(IrCmd::LOP_RETURN, build.constUint(0));
    });

    updateUseCounts(build.function);
    constantFold();

    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   STORE_INT R0, 4i
   LOP_RETURN 0u

bb_2:
   JUMP bb_3

bb_3:
   LOP_RETURN 1u

bb_4:
   JUMP bb_5

bb_5:
   LOP_RETURN 1u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "Guards")
{
    withOneBlock([this](IrOp a) {
        build.inst(IrCmd::CHECK_TAG, build.constTag(tnumber), build.constTag(tnumber), a);
        build.inst(IrCmd::LOP_RETURN, build.constUint(0));
    });

    withOneBlock([this](IrOp a) {
        build.inst(IrCmd::CHECK_TAG, build.constTag(tnil), build.constTag(tnumber), a);
        build.inst(IrCmd::LOP_RETURN, build.constUint(0));
    });

    updateUseCounts(build.function);
    constantFold();

    CHECK("\n" + toString(build.function, /* includeDetails */ false) == R"(
bb_0:
   LOP_RETURN 0u

bb_2:
   JUMP bb_3

bb_3:
   LOP_RETURN 1u

)");
}

TEST_CASE_FIXTURE(IrBuilderFixture, "ControlFlowCmpNum")
{
    IrOp nan = build.inst(IrCmd::DIV_NUM, build.constDouble(0.0), build.constDouble(0.0));

    auto compareFold = [this](IrOp lhs, IrOp rhs, IrCondition cond, bool result) {
        IrOp instOp;
        IrInst instExpected;

        withTwoBlocks([&](IrOp a, IrOp b) {
            instOp = build.inst(IrCmd::JUMP_CMP_NUM, lhs, rhs, build.cond(cond), a, b);
            instExpected = IrInst{IrCmd::JUMP, result ? a : b};
        });

        updateUseCounts(build.function);
        constantFold();
        checkEq(instOp, instExpected);
    };

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::Equal, true);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::Equal, false);
    compareFold(nan, nan, IrCondition::Equal, false);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::NotEqual, false);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::NotEqual, true);
    compareFold(nan, nan, IrCondition::NotEqual, true);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::Less, false);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::Less, true);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::Less, false);
    compareFold(build.constDouble(1), nan, IrCondition::Less, false);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::NotLess, true);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::NotLess, false);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::NotLess, true);
    compareFold(build.constDouble(1), nan, IrCondition::NotLess, true);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::LessEqual, true);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::LessEqual, true);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::LessEqual, false);
    compareFold(build.constDouble(1), nan, IrCondition::LessEqual, false);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::NotLessEqual, false);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::NotLessEqual, false);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::NotLessEqual, true);
    compareFold(build.constDouble(1), nan, IrCondition::NotLessEqual, true);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::Greater, false);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::Greater, false);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::Greater, true);
    compareFold(build.constDouble(1), nan, IrCondition::Greater, false);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::NotGreater, true);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::NotGreater, true);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::NotGreater, false);
    compareFold(build.constDouble(1), nan, IrCondition::NotGreater, true);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::GreaterEqual, true);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::GreaterEqual, false);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::GreaterEqual, true);
    compareFold(build.constDouble(1), nan, IrCondition::GreaterEqual, false);

    compareFold(build.constDouble(1), build.constDouble(1), IrCondition::NotGreaterEqual, false);
    compareFold(build.constDouble(1), build.constDouble(2), IrCondition::NotGreaterEqual, true);
    compareFold(build.constDouble(2), build.constDouble(1), IrCondition::NotGreaterEqual, false);
    compareFold(build.constDouble(1), nan, IrCondition::NotGreaterEqual, true);
}

TEST_SUITE_END();