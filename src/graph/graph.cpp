#include "Graph.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <numeric>

using namespace std;

// === DEFINITION OF CLASS GRAPH METHODS ===
Graph::Graph() : V(0) {}

Graph::Graph(int vertices) : V(vertices) {
    adj.resize(vertices);
}

void Graph::add_edge(int u, int v) {
    if (u == v) return;
    // Kiểm tra biên để đảm bảo không bị tràn chỉ số mảng
    if (u >= V || v >= V || u < 0 || v < 0) return; 
    
    adj[u].insert(v);
    adj[v].insert(u);
}

bool Graph::has_edge(int u, int v) const {
    if (u >= V || v >= V || u < 0 || v < 0) return false;
    return adj[u].count(v) > 0;
}

int Graph::num_vertices() const { 
    return V; 
}

Graph Graph::complement() const {
    Graph comp(this->V);
    for (int u = 0; u < V; u++) {
        // Tối ưu: Đồ thị vô hướng đối xứng nên chỉ cần xét v từ u + 1
        for (int v = u + 1; v < V; v++) {
            if (!has_edge(u, v)) {
                comp.add_edge(u, v);
            }
        }
    }
    return comp;
}

int Graph::degree(int v) const {
    if (v >= V || v < 0) return 0;
    return adj[v].size();
}

std::vector<int> Graph::nodes() const {
    vector<int> a(this->V);
    iota(a.begin(), a.end(), 0);
    return a;
};

const unordered_set<int>& Graph::neighbors(int v) const {
    return adj[v];
}
// Paser file dimacs
Graph parser_dimacs_col(const string& file_path, bool zero_based) {
    ifstream in(file_path);
    if (!in) {
        throw runtime_error("Cannot open file: " + file_path);
    }

    Graph graph;
    bool graph_initialized = false;
    string line;
    int line_num = 0;

    while (getline(in, line)) {
        ++line_num;
        
        if (line.empty()) continue;
        
        istringstream iss(line);
        vector<string> tokens;
        string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        
        if (tokens.empty()) continue;
        
        // Bỏ qua dòng comment
        if (tokens[0] == "c") {
            continue;
        }
        
        // Problem line: p edge <num_vertices> <num_edges>
        if (tokens[0] == "p") {
            if (tokens.size() != 4) {
                throw runtime_error("Invalid problem line at line " + to_string(line_num) 
                    + ": expected 4 tokens, got " + to_string(tokens.size()));
            }
            if (tokens[1] != "edge") {
                throw runtime_error("Invalid problem line at line " + to_string(line_num) 
                    + ": expected 'edge', got '" + tokens[1] + "'");
            }
            
            int num_vertices = stoi(tokens[2]);
            graph = Graph(num_vertices);
            graph_initialized = true;
            continue;
        }
        
        // Edge line: e <u> <v>
        if (tokens[0] == "e") {
            if (tokens.size() != 3) {
                throw runtime_error("Invalid edge line at line " + to_string(line_num) 
                    + ": expected 3 tokens, got " + to_string(tokens.size()));
            }
            if (!graph_initialized) {
                throw runtime_error("Edge line at line " + to_string(line_num) 
                    + " before problem line");
            }
            
            int u = stoi(tokens[1]);
            int v = stoi(tokens[2]);
            
            if (zero_based) {
                u -= 1;
                v -= 1;
            }
            
            graph.add_edge(u, v);
            continue;
        }
    }
    
    if (!graph_initialized) {
        throw runtime_error("No problem line found in file");
    }
    
    return graph;
}

