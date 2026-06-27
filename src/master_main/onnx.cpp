#include "onnx.h"
#include "vertex_features.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {

constexpr int kVertexFeatureCount = 7;

vector<int> order_by_scores(const vector<float>& scores) {
    vector<int> order(scores.size());
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        if (scores[lhs] != scores[rhs]) return scores[lhs] > scores[rhs];
        return lhs < rhs;
    });
    return order;
}

} // namespace

unique_ptr<OnnxSession> load_onnx_session(
    const string& model_path,
    const Graph& G
) {
    if (model_path.empty() || !fs::exists(model_path)) return nullptr;

    auto s = make_unique<OnnxSession>();
    try {
        s->env = make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "dp");
        Ort::SessionOptions opts;
        wstring wide(model_path.begin(), model_path.end());
        s->session = make_unique<Ort::Session>(*s->env, wide.c_str(), opts);
        cout << "ONNX model loaded: " << model_path << "\n";
    } catch (const exception& e) {
        cerr << "ONNX load failed: " << e.what() << " -- AI ordering disabled.\n";
        return nullptr;
    }

    // Build complement graph directed edge_index
    int n = G.num_vertices();
    vector<int64_t> src, tgt;
    for (int u = 0; u < n; ++u) {
        for (int v = u + 1; v < n; ++v) {
            if (!G.has_edge(u, v)) {
                src.push_back(u); tgt.push_back(v);
                src.push_back(v); tgt.push_back(u);
            }
        }
    }
    s->edge_index.insert(s->edge_index.end(), src.begin(), src.end());
    s->edge_index.insert(s->edge_index.end(), tgt.begin(), tgt.end());
    return s;
}

vector<int> ordering_vertices(
    OnnxSession& onnx,
    const Graph& G,
    const vector<double>& dual_value,
    const ColumnPool& pool
) {
    int n = G.num_vertices();
    if (n == 0) return {};

    // Collect raw (unnormalized) feature values
    vector<float> raw_degree(n), raw_norm_degree(n), raw_dual(n),
                  raw_wnd(n), raw_comp_degree(n), raw_comp_dual(n);

    float total_dual_sum = 0.0F;
    for (int v = 0; v < n; ++v)
        total_dual_sum += v < static_cast<int>(dual_value.size())
            ? static_cast<float>(dual_value[v]) : 0.0F;

    for (int v = 0; v < n; ++v) {
        float deg          = static_cast<float>(G.degree(v));
        raw_degree[v]      = deg;
        raw_norm_degree[v] = n > 1 ? deg / static_cast<float>(n - 1) : 0.0F;
        raw_dual[v]        = v < static_cast<int>(dual_value.size())
                                ? static_cast<float>(dual_value[v]) : 0.0F;
        raw_wnd[v]         = static_cast<float>(
                                vf::weighted_neighbor_dual(G, dual_value, v));
        raw_comp_degree[v] = static_cast<float>(n - 1) - deg;
        raw_comp_dual[v]   = total_dual_sum - raw_dual[v] - raw_wnd[v];
    }

    // Min-max normalize (raw_norm_degree already in [0,1])
    raw_degree      = vf::minmax_normalize(raw_degree);
    raw_dual        = vf::minmax_normalize(raw_dual);
    raw_wnd         = vf::minmax_normalize(raw_wnd);
    raw_comp_degree = vf::minmax_normalize(raw_comp_degree);
    raw_comp_dual   = vf::minmax_normalize(raw_comp_dual);

    // occurrence_rate: fraction of pool columns containing each vertex
    vector<int> frequencies = vf::column_frequencies(pool, n);
    double pool_size_plus_one = static_cast<double>(pool.columns.size()) + 1.0;

    vector<float> features;
    features.reserve(static_cast<size_t>(n) * kVertexFeatureCount);
    for (int v = 0; v < n; ++v) {
        float occurrence_rate = static_cast<float>(
            static_cast<double>(frequencies[v]) / pool_size_plus_one);
        features.insert(features.end(), {
            raw_degree[v], raw_norm_degree[v], raw_dual[v],
            raw_wnd[v], raw_comp_degree[v], raw_comp_dual[v],
            occurrence_rate
        });
    }

    const auto& edge_index = onnx.edge_index;
    size_t directed_edge_count = edge_index.size() / 2;
    if (directed_edge_count == 0 || edge_index.size() % 2 != 0) {
        vector<float> fallback(n, 0.0F);
        for (int v = 0; v < n; ++v)
            fallback[v] = v < static_cast<int>(dual_value.size())
                ? static_cast<float>(dual_value[v]) : 0.0F;
        return order_by_scores(fallback);
    }

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    array<int64_t, 3> input_shape = {1, static_cast<int64_t>(n), kVertexFeatureCount};
    array<int64_t, 2> edge_shape  = {2, static_cast<int64_t>(directed_edge_count)};

    vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<float>(
        mem, features.data(), features.size(),
        input_shape.data(), input_shape.size()));
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem, const_cast<int64_t*>(edge_index.data()), edge_index.size(),
        edge_shape.data(), edge_shape.size()));

    array<const char*, 2> input_names  = {"input_graph", "edge_index"};
    array<const char*, 1> output_names = {"output_graph"};

    auto outputs = onnx.session->Run(
        Ort::RunOptions{nullptr},
        input_names.data(), inputs.data(), inputs.size(),
        output_names.data(), output_names.size());

    if (outputs.empty() || !outputs[0].IsTensor())
        throw runtime_error("ONNX: expected output_graph tensor");
    if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != static_cast<size_t>(n))
        throw runtime_error("ONNX: output_graph element count != vertex count");

    const float* scores = outputs[0].GetTensorData<float>();
    vector<float> score_vec(scores, scores + n);
    return order_by_scores(score_vec);
}