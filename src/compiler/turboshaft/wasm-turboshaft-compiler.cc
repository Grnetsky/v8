// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-turboshaft-compiler.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turbofan-graph-visualizer.h"
// TODO(14108): Remove.
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::compiler::turboshaft {

wasm::WasmCompilationResult ExecuteTurboshaftWasmCompilation(
    wasm::CompilationEnv* env, compiler::WasmCompilationData& data,
    wasm::WasmDetectedFeatures* detected, Counters* counters) {
  // TODO(nicohartmann): We should not allocate TurboFan graph(s) here but
  // instead use only Turboshaft inside `GenerateWasmCodeFromTurboshaftGraph`.
  Zone zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME, kCompressGraphZone);
  compiler::MachineGraph* mcgraph = zone.New<compiler::MachineGraph>(
      zone.New<compiler::Graph>(&zone), zone.New<CommonOperatorBuilder>(&zone),
      zone.New<MachineOperatorBuilder>(
          &zone, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements()));

  OptimizedCompilationInfo info(
      GetDebugName(&zone, env->module, data.wire_bytes_storage,
                   data.func_index),
      &zone, CodeKind::WASM_FUNCTION);

  if (info.trace_turbo_json()) {
    TurboCfgFile tcf;
    tcf << AsC1VCompilation(&info);
  }

  if (info.trace_turbo_json()) {
    data.node_origins = zone.New<NodeOriginTable>(mcgraph->graph());
  }

  data.source_positions =
      mcgraph->zone()->New<SourcePositionTable>(mcgraph->graph());
  auto call_descriptor = GetWasmCallDescriptor(&zone, data.func_body.sig);

  if (!Pipeline::GenerateWasmCodeFromTurboshaftGraph(
          &info, env, data, mcgraph, detected, call_descriptor)) {
    return {};
  }

  if (counters && data.body_size() >= 100 * KB) {
    size_t zone_bytes = mcgraph->graph()->zone()->allocation_size();
    counters->wasm_compile_huge_function_peak_memory_bytes()->AddSample(
        static_cast<int>(zone_bytes));
  }

  auto result = info.ReleaseWasmCompilationResult();
  CHECK_NOT_NULL(result);  // Compilation expected to succeed.
  DCHECK_EQ(wasm::ExecutionTier::kTurbofan, result->result_tier);
  DCHECK_NULL(result->assumptions);
  result->assumptions = std::move(data.assumptions);
  DCHECK_IMPLIES(result->assumptions, !result->assumptions->empty());
  return std::move(*result);
}

}  // namespace v8::internal::compiler::turboshaft
