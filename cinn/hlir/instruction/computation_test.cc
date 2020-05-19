#include "cinn/hlir/instruction/computation.h"

#include <gtest/gtest.h>

#include <iostream>

namespace cinn {
namespace hlir {
namespace instruction {

TEST(Computation, basic) {
  Context context;
  cinn::Var N("N");
  Computation::Builder builder(&context, "default_computation");
  ParameterConfig parameter_config = {Float(32)};
  auto x = builder.AddInstruction(Instruction::CreateParameter(0, Shape({N, 30, 40}), "x", parameter_config));
  auto w = builder.AddInstruction(Instruction::CreateParameter(1, Shape({40, 50}), "w", parameter_config));

  auto dot0     = builder.AddInstruction(Instruction::CreateDot(x, w), "DOT");
  auto constant = builder.AddInstruction(Instruction::CreateConstant(Shape({1}), {1}, {Float(32)}), "constant 1");
  auto add      = builder.AddInstruction(Instruction::CreateBinary(InstrCode::Add, dot0, constant));

  auto computation = builder.Build();

  std::cout << "computation:\n\n" << computation->to_debug_string() << std::endl;
}

}  // namespace instruction
}  // namespace hlir
}  // namespace cinn
