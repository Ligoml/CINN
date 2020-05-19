#include "cinn/hlir/instruction/compiler.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "cinn/backends/codegen_c.h"
#include "cinn/hlir/instruction/module.h"
#include "cinn/runtime/cinn_runtime.h"

namespace cinn {
namespace hlir {
namespace instruction {

auto CreateTestBuffer(int kM, int kN) {
  auto* A = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {kM, kN}, 32);
  auto* B = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {kM, kN}, 32);
  auto* C = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {kM, kN}, 32);
  cinn_buffer_malloc(nullptr, A);
  cinn_buffer_malloc(nullptr, B);
  cinn_buffer_malloc(nullptr, C);
  float* Ad = reinterpret_cast<float*>(A->host_memory);
  float* Bd = reinterpret_cast<float*>(B->host_memory);

  for (int i = 0; i < A->num_elements(); i++) {
    Ad[i] = i;
    Bd[i] = i;
  }

  float* Cd = reinterpret_cast<float*>(C->host_memory);
  CHECK_EQ(C->num_elements(), A->num_elements());

  return std::make_tuple(A, B, C);
}

std::unique_ptr<Module> CreateModule(const std::string& name) {
  std::unique_ptr<Module> module(new Module(name));
  Context context;

  const char* fn_name = "elementwise_add0";

  {  // computation 0
    Computation::Builder builder(&context, fn_name);
    auto* x   = builder.AddInstruction(Instruction::CreateParameter(0, Shape({100, 200}), "X", {Float(32)}));
    auto* y   = builder.AddInstruction(Instruction::CreateParameter(1, Shape({100, 200}), "Y", {Float(32)}));
    auto* out = builder.AddInstruction(Instruction::CreateBinary(InstrCode::Add, x, y, Shape({100, 200})));
    out->set_inlined(false);  // the output should not be inlined

    module->AddComputation(builder.Build());
  }

  {  // computation main
    auto* target_computation = module->LookupComputation(fn_name);
    CHECK(target_computation);

    Computation::Builder builder(&context, "main");
    auto* x     = builder.AddInstruction(Instruction::CreateParameter(0, Shape({100, 200}), "X", {Float(32)}));
    auto* y     = builder.AddInstruction(Instruction::CreateParameter(1, Shape({100, 200}), "Y", {Float(32)}));
    auto* call0 = builder.AddInstruction(
        Instruction::CreateCall({x, y}, "out", Shape({100, 200}), Float(32), target_computation));
    auto* out_tuple = builder.AddInstruction(Instruction::CreateTuple(call0));
    auto* out       = builder.AddInstruction(Instruction::CreateTupleGet(out_tuple, 0));

    module->AddEntryComputation(builder.Build());
  }

  return module;
}

TEST(Compiler, call_kernel_directly) {
  Compiler compiler;

  auto module = CreateModule("module0");

  auto [a, b, c] = CreateTestBuffer(100, 200);  // NOLINT

  cinn_pod_value_t a_arg(a), b_arg(b), c_arg(c);
  cinn_pod_value_t args[] = {a_arg, b_arg, c_arg};

  compiler.Eval(module.get(), args, 3, "elementwise_add0");

  const float* c_data = reinterpret_cast<float*>(c->host_memory);

  for (int i = 0; i < c->num_elements(); i++) {
    ASSERT_EQ(c_data[i], i * 2);
  }

  delete a->host_memory;
  delete b->host_memory;
  delete c->host_memory;
}

TEST(Compiler, call_main) {
  Compiler compiler;

  auto module = CreateModule("module0");

  auto [a, b, c] = CreateTestBuffer(100, 200);  // NOLINT

  cinn_print_debug_string("a.host_memory: %p", a->host_memory);
  cinn_print_debug_string("b.host_memory: %p", b->host_memory);
  cinn_print_debug_string("c.host_memory: %p", c->host_memory);

  cinn_pod_value_t a_arg(a), b_arg(b), c_arg(c);
  cinn_pod_value_t args[] = {a_arg, b_arg, c_arg};

  compiler.Eval(module.get(), args, 3, "");

  const float* c_data = reinterpret_cast<float*>(c->host_memory);

  for (int i = 0; i < c->num_elements(); i++) {
    ASSERT_EQ(c_data[i], i * 2);
  }

  delete a->host_memory;
  delete b->host_memory;
  delete c->host_memory;
}

TEST(Compiler, call_main1) {
  Compiler compiler;

  auto module = CreateModule("module0");

  auto [a, b, c] = CreateTestBuffer(100, 200);  // NOLINT

  cinn_print_debug_string("a.host_memory: %p", a->host_memory);
  cinn_print_debug_string("b.host_memory: %p", b->host_memory);
  cinn_print_debug_string("c.host_memory: %p", c->host_memory);

  cinn_pod_value_t a_arg(a), b_arg(b), c_arg(c);
  cinn_pod_value_t args[] = {a_arg, b_arg, c_arg};

  compiler.Eval(module.get(), args, 3, "");

  const float* c_data = reinterpret_cast<float*>(c->host_memory);

  for (int i = 0; i < c->num_elements(); i++) {
    ASSERT_EQ(c_data[i], i * 2);
  }

  cinn_buffer_free(nullptr, a);
  cinn_buffer_free(nullptr, b);
  cinn_buffer_free(nullptr, c);
}

// W dot W + bias
Instruction* CreateDenseComputation(Computation::Builder* builder, Instruction* X, Instruction* W, Instruction* bias) {
  // inference the output shape, here we just consider the matrix multiplication case
  CHECK(std::get<int>(X->shape()[2]) == std::get<int>(W->shape()[0]));

  Shape out_shape({X->shape()[0], X->shape()[1], W->shape()[1]});

  auto* out_instr = builder->AddInstruction(Instruction::CreateDot(X, W, out_shape));
  if (bias) {
    out_instr = builder->AddInstruction(Instruction::CreateBinary(InstrCode::Add, out_instr, bias, out_shape));
  }
  return out_instr;
}

/*
 * A Fully Connected layer implementation with precision test, it has a dynamic Var batch_size, which simulates the
 * real-world FC layer behavior.
 */
TEST(Compiler, call_main_dense_model) {
  Compiler compiler;

  // create the model

  std::unique_ptr<Module> module(new Module("module0"));
  Context context;

  // Here, we include the dynamic determined variable, the batch size.
  cinn::Var batch_size(context.new_var_name("N"));
  // const int batch_size = 100;
  const int M = 200;
  const int N = 301;
  const int K = 423;

  Shape x_shape({batch_size, M, K});
  Shape w_shape({K, N});
  Shape out_shape({batch_size, M, N});
  Shape bias_shape({N});

  const char* fn_name = "mat_mul";

  ParameterConfig pconfig{Float(32)};

  {  // computation 0, the fully_connected layer
    Computation::Builder builder(&context, fn_name);
    auto* x = builder.AddInstruction(Instruction::CreateParameter(0, x_shape, "X", pconfig));
    auto* w = builder.AddInstruction(Instruction::CreateParameter(1, w_shape, "W", pconfig));
    auto* b = builder.AddInstruction(Instruction::CreateParameter(2, bias_shape, "Bias", pconfig));

    auto* out_instr = CreateDenseComputation(&builder, x, w, b);
    module->AddComputation(builder.Build());
  }

  {  // computation main
    Computation::Builder builder(&context, "main");
    auto* x = builder.AddInstruction(Instruction::CreateParameter(0, x_shape, "X", pconfig));
    auto* w = builder.AddInstruction(Instruction::CreateParameter(1, w_shape, "W", pconfig));
    auto* b = builder.AddInstruction(Instruction::CreateParameter(2, bias_shape, "Bias", pconfig));

    auto* call_instr = builder.AddInstruction(
        Instruction::CreateCall({x, w, b}, "out", out_shape, Float(32), module->LookupComputation(fn_name)));
    auto* tuple  = builder.AddInstruction(Instruction::CreateTuple(call_instr));
    auto* output = builder.AddInstruction(Instruction::CreateTupleGet(tuple, 0));
    module->AddEntryComputation(builder.Build());
  }

  cinn_buffer_t *Xb, *Wb, *Biasb, *Outb, *Outb_target;
  const int batch_size_runtime = 20;

  auto handcraft_compu = [&] {
    auto* xb_data    = reinterpret_cast<float*>(Xb->host_memory);
    auto* Wb_data    = reinterpret_cast<float*>(Wb->host_memory);
    auto* Biasb_data = reinterpret_cast<float*>(Biasb->host_memory);
    auto* Outb_data  = reinterpret_cast<float*>(Outb_target->host_memory);

    for (int b = 0; b < batch_size_runtime; b++) {
      for (int m = 0; m < M; m++) {
        for (int k = 0; k < K; k++) {
          for (int n = 0; n < N; n++) {
            Outb_data[b * (M * N) + m * N + n] += xb_data[b * (M * K) + m * K + k] * Wb_data[k * N + n];
          }
        }
      }
    }

    for (int b = 0; b < batch_size_runtime; b++) {
      for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
          Outb_data[b * M * N + m * N + n] += Biasb_data[n];
        }
      }
    }
  };

  {  // create buffer;
    Xb    = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {batch_size_runtime, M, K}, 32);
    Wb    = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {K, N}, 32);
    Biasb = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {N}, 32);
    Outb  = cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {batch_size_runtime, M, N}, 32);
    Outb_target =
        cinn_buffer_t::new_(cinn_device_kind_t::cinn_x86_device, cinn_float32_t(), {batch_size_runtime, M, N}, 32);

    auto randomize_buffer = [](cinn_buffer_t* buffer) {
      cinn_buffer_malloc(nullptr, buffer);
      auto* data = reinterpret_cast<float*>(buffer->host_memory);
      // for (int i = 0; i < buffer->num_elements(); i++) data[i] = static_cast<float>(rand()) / RAND_MAX;
      for (int i = 0; i < buffer->num_elements(); i++) data[i] = 1;
    };
    auto initialize_buffer = [](cinn_buffer_t* buffer) {
      cinn_buffer_malloc(nullptr, buffer);
      auto* data = reinterpret_cast<float*>(buffer->host_memory);
      memset(data, 2, buffer->num_elements() * sizeof(float));
    };

    randomize_buffer(Xb);
    randomize_buffer(Wb);
    randomize_buffer(Biasb);
    initialize_buffer(Outb);
    initialize_buffer(Outb_target);

    cinn_pod_value_t args[] = {cinn_pod_value_t(batch_size_runtime),
                               cinn_pod_value_t(Xb),
                               cinn_pod_value_t(Wb),
                               cinn_pod_value_t(Biasb),
                               cinn_pod_value_t(Outb)};
    auto fn                 = compiler.Compile(module.get());
    // fn = compiler.Lookup(fn_name);
    ASSERT_TRUE(fn);

    fn(args, 5);
  }

  {  // check result
    handcraft_compu();

    auto* out_data        = reinterpret_cast<float*>(Outb->host_memory);
    auto* out_target_data = reinterpret_cast<float*>(Outb_target->host_memory);

    for (int b = 0; b < batch_size_runtime; b++) {
      for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
          ASSERT_NEAR(out_data[b * M * N + m * N + n], out_target_data[b * M * N + m * N + n], 1e-5);
        }
      }
    }
  }

  delete Xb;
  delete Wb;
  delete Biasb;
  delete Outb;
  delete Outb_target;
}

}  // namespace instruction
}  // namespace hlir
}  // namespace cinn
