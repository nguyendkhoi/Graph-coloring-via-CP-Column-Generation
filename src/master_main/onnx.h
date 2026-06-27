#pragma once

#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct OnnxSession {
    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
    std::vector<int64_t>          edge_index;
};

// Load ONNX model and build edge_index from G.
// Returns nullptr if model_path empty/missing or loading fails.
std::unique_ptr<OnnxSession> load_onnx_session(
    const std::string& model_path,
    const Graph& G
);

// Run the GIN-based ONNX model to produce a vertex ordering.
std::vector<int> ordering_vertices(
    OnnxSession& onnx,
    const Graph& G,
    const std::vector<double>& dual_value,
    const ColumnPool& pool
);
