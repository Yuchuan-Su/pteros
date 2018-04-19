#pragma once

#include "pteros/core/selection.h"
#include "pteros/core/logging.h"
#include "pteros/core/utilities.h"
#include <Eigen/Core>
#include <set>

namespace pteros {

struct Mol_node {
    Mol_node(int _par, int _ind, int _el): parent(_par), ind(_ind), element(_el) {}
    std::shared_ptr<Mol_node> add(int i, int e);
    void print(int tab=0);
    void get_ind_vector(std::vector<int>& v);

    int ind;
    int element;
    int parent;
    std::vector<std::shared_ptr<Mol_node>> children;
};

typedef std::shared_ptr<Mol_node> Mol_node_ptr;

//-------------------------------------------

class Topmatch {
public:
    Topmatch(){}
    Topmatch(const Selection& sel);
    void set_source(const Selection& sel);
    // Match another molecule against this one
    bool match(const Selection& sel);
    // Match molecule against itself to determine symmetry
    //int match_self();

    std::vector<int> get_mapping();

private:
    void build_tree(Mol_node_ptr& node);
    bool build_match(Mol_node_ptr& node, Mol_node_ptr& ref);

    std::vector<std::vector<int>> con, m_con;
    Selection* p_sel;
    std::set<int> used;
    Mol_node_ptr root, m_root;
};

}
