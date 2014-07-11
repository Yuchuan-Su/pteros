/*
 *
 *                This source code is part of
 *                    ******************
 *                    ***   Pteros   ***
 *                    ******************
 *                 molecular modeling library
 *
 * Copyright (c) 2009-2013, Semen Yesylevskyy
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of Artistic License:
 *
 * Please note, that Artistic License is slightly more restrictive
 * then GPL license in terms of distributing the modified versions
 * of this software (they should be approved first).
 * Read http://www.opensource.org/licenses/artistic-license-2.0.php
 * for details. Such license fits scientific software better then
 * GPL because it prevents the distribution of bugged derivatives.
 *
*/

#include "pteros/core/selection_parser.h"
#include "pteros/core/system.h"
#include "pteros/core/selection.h"
#include "pteros/core/pteros_error.h"
#include "pteros/core/grid_search.h"
#include <Eigen/Core>
#include <cctype>
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/variant.hpp>
#include <unordered_set>
#include <regex>

//-----------------------------------------------------
//  Functions for creating actual selection from AST
//-----------------------------------------------------
using namespace std;
using namespace pteros;
using namespace boost;

#ifdef _DEBUG_PARSER
char* tok_names[] = {
    "TOK_VOID",
    "TOK_MINUS",
    "TOK_UNARY_MINUS",
    "TOK_PLUS",
    "TOK_MULT",
    "TOK_DIV",
    "TOK_EQ", // == or =
    "TOK_NEQ", // <> or !=
    "TOK_LT", //<
    "TOK_GT", //>
    "TOK_LEQ", //<=
    "TOK_GEQ", //>=
    // Operations for atom field codes
    "TOK_X",
    "TOK_Y",
    "TOK_Z",
    "TOK_OCC",
    "TOK_BETA",
    // Logic
    "TOK_OR",
    "TOK_AND",
    // Prefixes
    "TOK_NOT",
    "TOK_WITHIN",
    "TOK_PERIODIC",
    "TOK_OF",
    "TOK_BY",
    "TOK_RESIDUE",
    // text keywords
    "TOK_NAME",
    "TOK_RESNAME",
    "TOK_TAG",
    "TOK_TYPE",
    "TOK_CHAIN",
    // int keywords
    "TOK_RESID",
    "TOK_INDEX",
    "TOK_RESINDEX",
    // all
    "TOK_ALL",
    // Range
    "TOK_TO", // '-' or 'to'
    // Data "TOKens
    "TOK_INT",
    "TOK_FLOAT",
    "TOK_STR",
    // Parens
    "TOK_LPAREN",
    "TOK_RPAREN",
    // Distances
    "TOK_DIST",
    "TOK_POINT",
    "TOK_VECTOR",
    "TOK_PLANE",

    "TOK_PRECOMPUTED",
    "TOK_REGEX"
};


void AstNode::dump(int indent){
    for(int i=0;i<indent;++i) cout << '\t';
    cout << tok_names[code] << "{" << endl;
    if(code == TOK_PRECOMPUTED){
        //for(int j=0;j<precomputed.size();++j) cout << precomputed[j]<<" ";
        for(int i=0;i<indent;++i) cout << '\t';
        cout << "\tsz: " << precomputed.size() << endl;
    }

    for(int i=0;i<children.size();++i){        
        AstNode_ptr p;
        try {
            p = boost::get<AstNode_ptr>(children[i]);
            p->dump(indent+1);
        } catch(boost::bad_get) {
            for(int j=0;j<indent;++j) cout << '\t';
            cout << "  " << children[i] << endl;
        }
    }
    for(int i=0;i<indent;++i) cout << '\t';
    cout << "}" << endl;
}

string AstNode::decode(){
    return tok_names[code];
}

#endif

int AstNode::child_as_int(int i){    
    return boost::get<int>(boost::get<AstNode_ptr>(children[i])->children[0]);
}

bool AstNode::child_as_bool(int i){
    return boost::get<bool>(boost::get<AstNode_ptr>(children[i])->children[0]);
}

string AstNode::child_as_str(int i){    
    return boost::get<string>(boost::get<AstNode_ptr>(children[i])->children[0]);
}

char AstNode::child_as_char(int i){
    return boost::get<char>(boost::get<AstNode_ptr>(children[i])->children[0]);
}

float AstNode::child_as_float(int i){        
    return boost::get<float>(boost::get<AstNode_ptr>(children[i])->children[0]);
}

float AstNode::child_as_float_or_int(int i){    
    float d;
    try {
        d = child_as_float(i);
    } catch(boost::bad_get){
        d = child_as_int(i);
    }
    return d;
}

AstNode_ptr& AstNode::child_node(int i){
    return boost::get<AstNode_ptr>(children[i]);
}

bool AstNode::is_coordinate_dependent(){
    if(code == TOK_X || code == TOK_Y || code == TOK_Z || code == TOK_WITHIN
            || code == TOK_POINT || code == TOK_PLANE || code == TOK_VECTOR
      ){
        return true;
    } else {
        return false;
    }
}


Selection_parser::Selection_parser(){
    has_coord = false;
}

Selection_parser::~Selection_parser(){}

// Recognizer
AstNode_ptr recognize(string s){
    AstNode_ptr node(new AstNode);
    string str(s);
    to_lower(s);
    if(s=="+"){
        node->code = TOK_PLUS;
    } else if(s=="*"){
        node->code = TOK_MULT;
    } else if(s=="/"){
        node->code = TOK_DIV;    
    } else if(s== "-"){
        node->code = TOK_MINUS;
    } else if(s== "("){
        node->code = TOK_LPAREN;
    } else if(s== ")"){
        node->code = TOK_RPAREN;
    } else if(s== "=" || s=="=="){
        node->code = TOK_EQ;
    } else if(s== "<>" || s=="!="){
        node->code = TOK_NEQ;
    } else if(s== "<"){
        node->code = TOK_LT;
    } else if(s== ">"){
        node->code = TOK_GT;
    } else if(s== "<="){
        node->code = TOK_LEQ;
    } else if(s== ">="){
        node->code = TOK_GEQ;
    } else if(s== "x"){
        node->code = TOK_X;
    } else if(s== "y"){
        node->code = TOK_Y;
    } else if(s== "z"){
        node->code = TOK_Z;
    } else if(s== "occupancy"){
        node->code =TOK_OCC;
    } else if(s== "beta"){
        node->code = TOK_BETA;
    } else if(s== "or"){
        node->code = TOK_OR;
    } else if(s== "and"){
        node->code = TOK_AND;
    } else if(s== "not"){
        node->code = TOK_NOT;
    } else if(s== "within"){
        node->code = TOK_WITHIN;        
    } else if(s== "periodic" || s=="nonperiodic" || s=="pbc" || s=="nopbc"){
        node->code = TOK_PERIODIC;
        if(s== "periodic" || s=="pbc")
            node->children.push_back(true);
        else
            node->children.push_back(false);
    } else if(s== "of"){
        node->code = TOK_OF;
    } else if(s== "by"){
        node->code = TOK_BY;
    } else if(s=="res" || s=="residue"){
        node->code = TOK_RESIDUE;
    } else if(s== "name"){
        node->code = TOK_NAME;
    } else if(s== "resname"){
        node->code = TOK_RESNAME;
    } else if(s== "tag"){
        node->code = TOK_TAG;
    } else if(s== "chain"){
        node->code = TOK_CHAIN;
    } else if(s== "resid"){
        node->code = TOK_RESID;
    } else if(s== "index"){
        node->code = TOK_INDEX;
    } else if(s== "resindex"){
        node->code = TOK_RESINDEX;
    } else if(s== "all"){
        node->code = TOK_ALL;
    } else if(s== "to"){
        node->code = TOK_TO;
    } else if(s== "dist" || s== "distance"){
        node->code = TOK_DIST;
    } else if(s== "point"){
        node->code = TOK_POINT;
    } else if(s== "vector"){
        node->code = TOK_VECTOR;
    } else if(s== "plane"){
        node->code = TOK_PLANE;
    } else {
        // Try to convert token to int
        try {
            node->children.push_back( boost::lexical_cast<int>(str));
            node->code = TOK_INT;
        } catch(boost::bad_lexical_cast) {
            // Try to convert token to float
            try {
                node->children.push_back( boost::lexical_cast<float>(str) );
                node->code = TOK_FLOAT;                
            } catch(boost::bad_lexical_cast) {
                // ok, save it as a string
                node->children.push_back(str);
                // Check if this is a regex
                if( boost::algorithm::all(str,boost::algorithm::is_alnum()) ){
                    // Plane string
                    node->code = TOK_STR;
                } else {
                    // Regex
                    node->code = TOK_REGEX;
                }
            }
        }
    }
    return node;
}

// Safely get char respecting end of string
char at(const string& s, int i){
    if(i<s.size()) return s[i]; else return 0;
}

// End current token at index i
void Selection_parser::end_token(const string& s, int& b, int i){ // i is last included index
    if(b>=0 && i>=b && i<s.size()){ // Previous token exists, save it
        // Recognize new token
        tokens.push_back( recognize(s.substr(b,i+1-b)) );
        token_ends.push_back(i);
        // Reset starting position
        b = i+1;
    }
}

void Selection_parser::tokenize(const string& s){
    tokens.clear();

    int b = -1; // Begin of current token
    int cur = 0; // Current position

    // Cycle over characters and collect tokens
    while(cur<s.size()){
        // Classify this character
        if(!isspace(s[cur])){
            // If b<0 then start new token here, otherwise continue previous one
            if(b<0) b = cur;

            // See if we have "" or '' escaped sequence - it's possibly a regex
            if(s[cur]=='"' || s[cur]=='\''){
                end_token(s,b,cur-1);
                char delim = s[cur];
                cur += 1;
                b += 1;
                while(s[cur]!=delim) cur += 1;
                end_token(s,b,cur-1);
                cur += 1;
            }

            // Avoid recognition of - in scientific notation like 4.5e-5
            if( (s[cur]=='e' || s[cur]=='E') && at(s,cur+1)=='-' && isdigit(at(s,cur-1)) ){
                cur += 2;
                continue;
            }

            // See if this is one of single "punctuation" symbols
            // they can only come alone, so they are separate tokens
            if(s[cur]=='+' || s[cur]=='*' || s[cur]=='/' ||
               s[cur]=='(' || s[cur]==')' || s[cur]=='-'){
                end_token(s,b,cur-1);
                end_token(s,b,cur);
                cur += 1;
                continue;
            }
            // See if we have one of two-character operators, like >=
            if(s[cur]=='>'){
                end_token(s,b,cur-1);
                if(at(s,cur+1)=='='){
                    end_token(s,b,cur+1);
                    cur += 2;
                } else {
                    end_token(s,b,cur);
                    cur += 1;
                }
                continue;
            }

            if(s[cur]=='<'){
                end_token(s,b,cur-1);
                if(at(s,cur+1)=='=' || at(s,cur+1)=='>'){
                    end_token(s,b,cur+1);
                    cur += 2;
                } else {
                    end_token(s,b,cur);
                    cur += 1;
                }
                continue;
            }

            if(s[cur]=='='){
                end_token(s,b,cur-1);
                if(at(s,cur+1)=='='){
                    end_token(s,b,cur+1);
                    cur += 2;
                } else {
                    end_token(s,b,cur);
                    cur += 1;
                }
                continue;
            }

            if(s[cur]=='!' && at(s,cur+1)=='='){
                end_token(s,b,cur-1);
                end_token(s,b,cur+1);
                cur += 2;
                continue;
            }

        } else {
            // This is whitespace, token complete
            end_token(s,b,cur-1);
            b = -1;
        }

        // If we are here, then just silently increment cur
        ++cur;
    }
    // Add last token
    end_token(s,b,cur-1);

#ifdef _DEBUG_PARSER
    cout << endl <<"Tokenizer result:" << endl;
    std::shared_ptr<AstNode> node;
    for(auto node: tokens){
            cout << tok_names[node->code] << " ";
    }
    cout << endl << endl;
#endif
}

// Class used to recognize grammar rules
namespace pteros {
struct Grammar {
    int cur;
    Selection_parser* p;
    AstNode_ptr dum;

    Grammar(Selection_parser* parent){
        cur = 0; // Current token being processed
        p = parent;
    }

    // Tries to read next token of type c
    // If succesfull advances to one token
    // Returns true if found and sets match to matched token
    bool expect(Codes c, AstNode_ptr& match){
        if(cur>=p->tokens.size()) return false; // No more tokens
        if(p->tokens[cur]->code == c){
            match = p->tokens[cur];
            ++cur;
            return true;
        } else {
            return false;
        }
    }

    //--------------------------
    // Rules
    //--------------------------
    bool unary_minus(AstNode_ptr& res){
        AstNode_ptr val;
        if( expect(TOK_MINUS,res) && num_factor(val) ){
            res->children.push_back(val);
            res->code = TOK_UNARY_MINUS;
            return true;
        }
        return false;
    }

    bool num_factor(AstNode_ptr& res){
        bool ok =  expect(TOK_FLOAT,res)
                || expect(TOK_INT,res)
                || (expect(TOK_LPAREN,dum) && num_expr(res) && expect(TOK_RPAREN,dum))
                || expect(TOK_X,res)
                || expect(TOK_Y,res)
                || expect(TOK_Z,res)
                || expect(TOK_BETA,res)
                || expect(TOK_OCC,res)
                || distance_rule(res)
                || unary_minus(res);
        return ok;
    }


    bool distance_rule(AstNode_ptr& res){
        AstNode_ptr dum,pbc;
        bool ok =  expect(TOK_DIST,dum);

        if(!ok) return false;

        bool has_pbc = false;
        if(expect(TOK_PERIODIC,pbc)) has_pbc = true;

        if(expect(TOK_POINT,res)){
            AstNode_ptr x,y,z;
            ok = num_factor(x) && num_factor(y) && num_factor(z);
            if(!ok) return false;

            res->children.push_back(x);
            res->children.push_back(y);
            res->children.push_back(z);

            if(has_pbc) res->children.push_back(pbc);

        } else if(expect(TOK_VECTOR,res) || expect(TOK_PLANE,res)){
            AstNode_ptr x,y,z,dir1,dir2,dir3;
            ok = num_factor(x) && num_factor(y) && num_factor(z)
              && num_factor(dir1) && num_factor(dir2) && num_factor(dir3);
            if(!ok) return false;

            res->children.push_back(x);
            res->children.push_back(y);
            res->children.push_back(z);
            res->children.push_back(dir1);
            res->children.push_back(dir2);
            res->children.push_back(dir3);

            if(has_pbc) res->children.push_back(pbc);
        }
        return true;
    }


    bool num_term(AstNode_ptr& res){
        AstNode_ptr operand1,operand2,op;
        bool ok = num_factor(operand1);

        if(!ok) return false;

        res = operand1; // If no operators follow this will be passed out
        while( (expect(TOK_MULT,op) || expect(TOK_DIV,op)) && num_factor(operand2) ){
            if(res->children.size()==2){
                operand1 = res; // Move ready tree to the first operand
            }
            res = op;
            res->children.push_back( operand1 );
            res->children.push_back( operand2 );
        }
        return true;
    }

    bool num_expr(AstNode_ptr& res){
        AstNode_ptr operand1,operand2,op;
        bool ok = num_term(operand1);

        if(!ok) return false;

        res = operand1; // If no operators follow this will be passed out
        while( (expect(TOK_PLUS,op) || expect(TOK_MINUS,op)) && num_term(operand2) ){
            if(res->children.size()==2){
                operand1 = res; // Move ready tree to the first operand
            }
            res = op;
            res->children.push_back( operand1 );
            res->children.push_back( operand2 );
        }
        return true;
    }


    bool num_comparison(AstNode_ptr& res){
        AstNode_ptr operand1,operand2,op;
        bool ok = num_expr(operand1);
        if(!ok) return false;
        if( (  expect(TOK_EQ,op)  || expect(TOK_NEQ,op)
            || expect(TOK_LT,op)  || expect(TOK_GT,op)
            || expect(TOK_LEQ,op) || expect(TOK_GEQ,op)
            ) && num_expr(operand2)){
            // Both operands are present
            res = op;
            res->children.push_back( operand1 );
            res->children.push_back( operand2 );
            ok = true;
        } else ok = false;
        return ok;
    }

    bool logical_expr(AstNode_ptr& res){
        AstNode_ptr operand1,operand2,op;
        bool ok = logical_operand(operand1);

        if(!ok) return false;

        res = operand1; // If no operators follow this will be passed out
        while( (expect(TOK_OR,op) || expect(TOK_AND,op)) && logical_operand(operand2) ){
            if(res->children.size()==2){
                operand1 = res; // Move ready tree to the first operand
            }
            res = op;
            res->children.push_back( operand1 );
            res->children.push_back( operand2 );
        }
        return true;
    }


    bool logical_not(AstNode_ptr& res){
        AstNode_ptr tmp;
        bool ok = expect(TOK_NOT,tmp);
        if(!ok) return false;
        ok = logical_operand(res);
        if(ok){
            tmp->children.push_back( res );
            res = tmp;
        }
        return ok;
    }

    bool within_rule(AstNode_ptr& res){
        AstNode_ptr dist,expr,dim,periodic;
        bool ok,has_periodic = false;

        ok =  expect(TOK_WITHIN,res) && (expect(TOK_FLOAT,dist) || expect(TOK_INT,dist));
        if(!ok) return false;

        // Set periodicity if given
        if( expect(TOK_PERIODIC,periodic) ) has_periodic = true;

        ok = expect(TOK_OF,dum) && logical_operand(expr);

        if(ok){
            res->children.push_back( dist );
            res->children.push_back( expr );            
            if(has_periodic) res->children.push_back( periodic );
        }
        return ok;
    }

    bool by_residue(AstNode_ptr& res){
        AstNode_ptr expr;
        bool ok =  expect(TOK_BY,res) && expect(TOK_RESIDUE,dum)
                && logical_operand(expr);
        if(ok){
            res->children.push_back( expr );
        }
        return ok;
    }

    // rule of type "keyword text text text ..."
    bool keyword_text_list(AstNode_ptr& res){

        bool ok =  expect(TOK_NAME,res) || expect(TOK_RESNAME,res)
                || expect(TOK_TAG,res)  || expect(TOK_CHAIN,res);
        if(ok){

            AstNode_ptr str;
            // Read list of STR tokens
            while( expect(TOK_STR,str) || expect(TOK_REGEX,str) ){
                res->children.push_back( str );
            }
            if(res->children.size()==0) ok = false;
        }
        return ok;
    }

    // rule of type "keyword [int|range] [int|range]..."
    bool int_or_range(AstNode_ptr& res){
        AstNode_ptr v1,v2;
        if( expect(TOK_INT,v1) ){
            if( (expect(TOK_TO,res) || expect(TOK_MINUS,res)) && expect(TOK_INT,v2) ){
                res->code = TOK_TO;
                res->children.push_back( v1 );
                res->children.push_back( v2 );
            } else res = v1;
            return true;
        }
        return false;
    }

    bool keyword_int_list(AstNode_ptr& res){
        bool ok =  expect(TOK_RESID,res) || expect(TOK_RESINDEX,res)
                || expect(TOK_INDEX,res);
        if(ok){
            AstNode_ptr tok;
            // Read list of STR tokens
            while( int_or_range(tok) ) res->children.push_back( tok );
            if(res->children.size()==0) ok = false;
        }
        return ok;
    }

    bool logical_operand(AstNode_ptr& res){
        bool ok = (expect(TOK_LPAREN,dum) && logical_expr(res) && expect(TOK_RPAREN,dum))
                  || num_comparison(res)
                  || expect(TOK_ALL,res)
                  || logical_not(res)
                  || within_rule(res)                  
                  || by_residue(res)
                  || keyword_text_list(res)
                  || keyword_int_list(res);
        return ok;
    }

    // Evaluates top-level rule and return AST
    int run(AstNode_ptr& res){
        // Match top-level rule
        logical_expr(res);
        return cur;
    }

};
} //namespace


bool is_node_pure(AstNode_ptr& node){
    if(node->is_coordinate_dependent()) return false;

    for(int i=0;i<node->children.size();++i){
        try {
            if( is_node_pure(boost::get<AstNode_ptr>(node->children[i])) == false){
                return false;
            }
        } catch(boost::bad_get){};
    }

    return true;
}

void Selection_parser::create_ast(string& sel_str){
#ifdef _DEBUG_PARSER
	cout << "Going to create AST from: " << sel_str <<endl;
#endif
    tokenize(sel_str);
    Grammar gr(this);
    int pos = gr.run(tree);
    if(pos!=tokens.size()){
        string s("Syntax error in selection string here:\n\""+sel_str+"\"\n");
        for(int i=0;i<token_ends[pos];++i) s+="~";
        s+="^";
        throw Pteros_error(s);
    }
    // Now we can free tokens array. This will kill all unused nodes
    tokens.clear();
    // Also free aux error reporting positions
    token_ends.clear();

    if(!is_node_pure(tree)) has_coord = true;
    is_optimized = false; // Not yet optimized

#ifdef _DEBUG_PARSER
    cout << "Is coordinate dependent? " << has_coord << endl;
    tree->dump();
#endif
}



void Selection_parser::do_optimization(AstNode_ptr& node){

    // Skip optimization for trivial leaf nodes
    if(node->code == TOK_VOID
        || node->code == TOK_STR
        || node->code == TOK_REGEX
        || node->code == TOK_FLOAT
        || node->code == TOK_INT        
       ) return;

    // Now check if this node does not contain coord-dependent children
    if(is_node_pure(node)){
#ifdef _DEBUG_PARSER
        cout << "Node " << node->decode() << " is pure" << endl;
#endif
        // Unary minus nodes require special treatment
        if(node->code == TOK_UNARY_MINUS){
            // If the child of unary minus node is a number than apply minus to it and store
            // otherwise keep it as is
            Codes c = node->child_node(0)->code;
            if(c == TOK_INT){
                int val = node->child_as_int(0);
                node->children.clear();
                node->children.push_back(-val);
                node->code = TOK_INT;
            } else if(c == TOK_FLOAT){
                float val = node->child_as_float(0);
                node->children.clear();
                node->children.push_back(-val);
                node->code = TOK_FLOAT;
            }
        } else {
            // Node is pure, so clear all its children and keep precomputed index
            // Set node type to precomputed

            /*
            vector<int> res;
            eval_node(node,res,NULL);
            node->precomputed = res;
            */
            eval_node(node,node->precomputed,NULL);
            node->children.clear();
            node->code = TOK_PRECOMPUTED;

    #ifdef _DEBUG_PARSER
            cout << "Node set to precomputed " << endl;
    #endif
        }
    }

    // Optimize AND operations - coord-dependent operand
    // should go second to benefit from subspace optimization
    if(node->code == TOK_AND){
        try{            
            if(  !is_node_pure(node->child_node(0))
               && is_node_pure(node->child_node(1))
               ){

#ifdef _DEBUG_PARSER
                cout << "Node " << node->decode() << " swapped" << endl;
#endif

                //p1.swap(p2);
                //boost::get<AstNode_ptr>(node->children[0]).swap(boost::get<AstNode_ptr>(node->children[1]));
                node->child_node(0).swap(node->child_node(1));
             }
        } catch (boost::bad_get) {}
    }

    // Go deeper
    for(int i=0;i<node->children.size();++i)
        try {
            do_optimization(node->child_node(i));
        } catch(boost::bad_get){};

}


void Selection_parser::apply(System* system, size_t fr, vector<int>& result){
#ifdef _DEBUG_PARSER
	cout << "Applying to the system with first atom " << system->atoms[0].name << endl;
#endif

    sys = system;
    frame = fr;
    Natoms = sys->num_atoms();

    // For coordinate-dependent selections perform optimization
    // to precompute all pure not-coordinate-depenednt nodes
    if(has_coord && !is_optimized){

#ifdef _DEBUG_PARSER
        cout << "Tree before optimizaton:" << endl;
        tree->dump(0);
#endif
        do_optimization(tree);
        is_optimized = true;

#ifdef _DEBUG_PARSER
        cout << "Tree after optimizaton:" << endl;
        tree->dump(0);
#endif
    }

    // Eval root node
    eval_node(tree,result,NULL);

    // Sort result to always get ordered selection index
    sort(result.begin(),result.end());

#ifdef _DEBUG_PARSER
    int n = 0;
    for(int i=0;i<result.size();++i)
        cout << result[i] << " ";
    cout << endl;    
#endif    
}

void Selection_parser::eval_node(AstNode_ptr& node, vector<int>& result, vector<int>* subspace){
    int i,at,j,k,n;

    // Clear any garbage passed in result
    result.clear();

    switch(node->code){
    case  TOK_PRECOMPUTED: {
        if(!subspace){
            result = node->precomputed;
        } else {
            // If this node is under subspace we only need to return those
            // atoms, which are in that subspace. Otherwise we can extend
            // the subspace but we don't want this. Thus return intersection:
            std::set_intersection(subspace->begin(),subspace->end(),
                                  node->precomputed.begin(),node->precomputed.end(),
                                  back_inserter(result));
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_NOT: {
        // Logical NOT
        vector<int> res1;
        eval_node(node->child_node(0), res1, subspace);
        // Sort
        std::sort(res1.begin(),res1.end());
        // res1 is sorted, so we can speed up negation a bit by filling only the "gaps"
        n = res1.size();
        for(j=0;j<res1[0];++j) result.push_back(j); //Before first
        for(i=1;i<n;++i)
            for(j=res1[i-1]+1;j<res1[i];++j) result.push_back(j); // between any two
        for(j=res1[n-1]+1;j<Natoms;++j) result.push_back(j); // after last
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_OR: {
        // Logical OR
        vector<int> res1, res2; // Aux vectors

        eval_node(node->child_node(0), res1, subspace);
        eval_node(node->child_node(1), res2, subspace);

        // Sort
        std::sort(res1.begin(),res1.end());
        std::sort(res2.begin(),res2.end());

        std::set_union(res1.begin(),res1.end(),res2.begin(),res2.end(),back_inserter(result));
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_AND: {
        // Logical AND
        vector<int> res1, res2; // Aux vectors

        eval_node(node->child_node(0), res1, subspace);
        // First operand sets a subspace for the second!
        eval_node(node->child_node(1), res2, &res1);

        // Sort
        std::sort(res1.begin(),res1.end());
        std::sort(res2.begin(),res2.end());

        std::set_intersection(res1.begin(),res1.end(),res2.begin(),res2.end(),back_inserter(result));
        return;
    }

    // Coord-independent selections are evaluated only once thus
    // no need to bother with subspace optimizations

    //---------------------------------------------------------------------------
    case  TOK_NAME: {
        int Nchildren = node->children.size(); // Get number of children
        string str;
        // Cycle over children

        for(i=0;i<Nchildren;++i){
            str = node->child_as_str(i);
            if(node->child_node(i)->code == TOK_STR){
                // For normal strings
                for(at=0;at<Natoms;++at)
                    if(sys->atoms[at].name == str) result.push_back(at);
            } else if(node->child_node(i)->code == TOK_REGEX){
                // For regex
                std::cmatch what;
                std::regex reg(str);
                for(at=0;at<Natoms;++at){
                    if(std::regex_match(sys->atoms[at].name.c_str(),what,reg)){
                         result.push_back(at);
                    }
                }
            }
        }

        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_RESNAME: {
        int Nchildren = node->children.size(); // Get number of children
        string str;
        // Cycle over children
        for(i=0;i<Nchildren;++i){
            str = node->child_as_str(i);
            if(node->child_node(i)->code == TOK_STR){
                // For normal strings
                for(at=0;at<Natoms;++at)
                    if(sys->atoms[at].resname == str) result.push_back(at);
            } else if(node->child_node(i)->code == TOK_REGEX){
                // For regex
                std::cmatch what;
                std::regex reg(str);
                for(at=0;at<Natoms;++at){
                    if(std::regex_match(sys->atoms[at].resname.c_str(),what,reg)){
                         result.push_back(at);
                    }
                }
            }
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_TAG: {
        int Nchildren = node->children.size(); // Get number of children
        string str;
        // Cycle over children
        for(i=0;i<Nchildren;++i){
            str = node->child_as_str(i);
            if(node->child_node(i)->code == TOK_STR){
                // For normal strings
                for(at=0;at<Natoms;++at)
                    if(sys->atoms[at].tag == str) result.push_back(at);
            } else if(node->child_node(i)->code == TOK_REGEX){
                // For regex
                std::cmatch what;
                std::regex reg(str);
                for(at=0;at<Natoms;++at){
                    if(std::regex_match(sys->atoms[at].tag.c_str(),what,reg)){
                         result.push_back(at);
                    }
                }
            }
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_CHAIN: {
        int Nchildren = node->children.size(); // Get number of children
        char ch;
        // Cycle over children
        for(i=0;i<Nchildren;++i){
            ch = node->child_as_str(i)[0];
            for(at=0;at<Natoms;++at)
                if(sys->atoms[at].chain == ch) result.push_back(at);
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_RESID: {
        int Nchildren = node->children.size(); // Get number of children
        // Cycle over children
        for(i=0;i<Nchildren;++i){
            // Try to get integer. If successful, go to add atoms to selection
            try {
                k = node->child_as_int(i);
                for(at=0;at<Natoms;++at)
                    // Even if k is out of range, nothing will crash here
                    if(sys->atoms[at].resid == k) result.push_back(at);
            } catch(boost::bad_get) {
                // Exception thrown, which means that this is a range, not an integer
                AstNode_ptr range = node->child_node(i);
                int i1 = range->child_as_int(0);
                int i2 = range->child_as_int(1);
                for(k=i1;k<=i2;++k)
                    for(at=0;at<Natoms;++at)
                        // Even if k is out of range, nothing will crash here
                        if(sys->atoms[at].resid == k) result.push_back(at);
            }
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_RESINDEX: {
        int Nchildren = node->children.size(); // Get number of children
        // Cycle over children
        for(i=0;i<Nchildren;++i){
            // Try to get integer. If successful, go to add atoms to selection
            try {
                k = node->child_as_int(i);
                for(at=0;at<Natoms;++at)
                    // Even if k is out of range, nothing will crash here
                    if(sys->atoms[at].resindex == k) result.push_back(at);
            } catch(boost::bad_get) {
                // Exception thrown, which means that this is a range, not an integer
                AstNode_ptr range = node->child_node(i);
                int i1 = range->child_as_int(0);
                int i2 = range->child_as_int(1);
                for(k=i1;k<=i2;++k)
                    for(at=0;at<Natoms;++at)
                        // Even if k is out of range, nothing will crash here
                        if(sys->atoms[at].resindex == k) result.push_back(at);
            }
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_INDEX: {
        int Nchildren = node->children.size(); // Get number of children
        // Cycle over children
        for(i=0;i<Nchildren;++i){
            // Try to get integer. If successful, go to add atoms to selection
            try {
                k = node->child_as_int(i);
                // We have to check the range here
                if(k>=0 && k<Natoms)
                    result.push_back(k);
            } catch(boost::bad_get) {
                // Exception thrown, which means that this is a range, not an integer
                AstNode_ptr range = node->child_node(i);
                int i1 = range->child_as_int(0);
                int i2 = range->child_as_int(1);
                for(k=i1;k<=i2;++k)
                    // We have to check the range here
                    if(k>=0 && k<Natoms)
                        result.push_back(k);
            }
        }
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_WITHIN: {        
        // Get distance
        double dist = node->child_as_float_or_int(0);

#ifdef _DEBUG_PARSER
        if(subspace)
            cout << "subspace size: " << subspace->size() << endl;
        else
            cout << "full subspace!" << endl;
#endif
        // Create selections for searching
        Selection dum1(*sys), dum2(*sys);

        // Evaluate enclosed expression
        // Enclosed expression is independent on any subspace!
        // Otherwise the results would be incorrect        
        // Result is returned directly into the index array of selection dum2
        // thus no additional copying
        eval_node(node->child_node(1), dum2.index, NULL);

        bool periodic = false;
        // If we have children[2] then dimensions or/and periodicity are set
        if(node->children.size()==3){
            periodic = node->child_as_bool(2);
        }

        // Prepare selection dum1
        if(!subspace){
            // We are not limited by subspace
            dum1.index.resize(sys->num_atoms());
            for(int i=0;i<sys->num_atoms();++i) dum1.index[i] = i;
        } else {
            // We are limited by subspace
            dum1.index = *subspace;
        }        

        // Set frame for both selections
        dum1.set_frame(frame);
        dum2.set_frame(frame);

        Grid_searcher(dist,dum1,dum2,result,true,true,periodic);

        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_BY: {        
        vector<int> res1;
        // Evaluate enclosed expression, ok to pass subspace
        eval_node(node->child_node(0), res1, subspace);
        int Nsel = res1.size();
        // Select by residue. This respects chain!
        // First make a set of resids we need to search
        std::unordered_set<int> resind;
        for(i=0;i<Nsel;++i){ //over found atoms
            resind.insert(sys->atoms[res1[i]].resindex);
        }

        // Now cycle over all atoms in the system (not a subset!)
        for(at=0;at<Natoms;++at){ // over all atoms
            if(resind.count(sys->atoms[at].resindex)){
                // This resind is needed
                result.push_back(at);
            }
        }
        // Now result is sorted by default here and there are no duplicates
        return;
    }
    //---------------------------------------------------------------------------
    case  TOK_ALL:
        result.reserve(Natoms);
        for(at=0;at<Natoms;++at) result.push_back(at);
        return;          
    //---------------------------------------------------------------------------
    // Math logical nodes
    // All of them benefit from subspace optimization
    case  TOK_EQ:
        if(!subspace){
            for(at=0;at<Natoms;++at) // over all atoms
                if(eval_numeric(node->child_node(0),at) ==
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
        } else {
            for(int i=0;i<subspace->size();++i){ // over subspace
                at = (*subspace)[i];
                if(eval_numeric(node->child_node(0),at) ==
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        }
        return;
    //---------------------------------------------------------------------------
    case  TOK_NEQ:
        if(!subspace){
            for(at=0;at<Natoms;++at) // over all atoms
                if(eval_numeric(node->child_node(0),at) !=
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
        } else {
            for(int i=0;i<subspace->size();++i){ // over subspace
                at = (*subspace)[i];
                if(eval_numeric(node->child_node(0),at) !=
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        }
        return;
    //---------------------------------------------------------------------------
    case  TOK_LT:
        if(!subspace){            
            for(at=0;at<Natoms;++at){ // over all atoms
                if(eval_numeric(node->child_node(0),at) <
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        } else {            
            for(int i=0;i<subspace->size();++i){ // over subspace
                at = (*subspace)[i];
                if(eval_numeric(node->child_node(0),at) <
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        }
        return;
    //---------------------------------------------------------------------------
    case  TOK_GT:
        if(!subspace){
            for(at=0;at<Natoms;++at) // over all atoms
                if(eval_numeric(node->child_node(0),at) >
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
        } else {
            for(int i=0;i<subspace->size();++i){ // over subspace
                at = (*subspace)[i];
                if(eval_numeric(node->child_node(0),at) >
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        }
        return;
    //---------------------------------------------------------------------------
    case  TOK_LEQ:
        if(!subspace){
            for(at=0;at<Natoms;++at) // over all atoms
                if(eval_numeric(node->child_node(0),at) <=
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
        } else {
            for(int i=0;i<subspace->size();++i){ // over subspace
                at = (*subspace)[i];
                if(eval_numeric(node->child_node(0),at) <=
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        }
        return;
    //---------------------------------------------------------------------------
    case  TOK_GEQ:
        if(!subspace){
            for(at=0;at<Natoms;++at) // over all atoms
                if(eval_numeric(node->child_node(0),at) >=
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
        } else {
            for(int i=0;i<subspace->size();++i){ // over subspace
                at = (*subspace)[i];
                if(eval_numeric(node->child_node(0),at) >=
                   eval_numeric(node->child_node(1),at)
                  ) result.push_back(at);
            }
        }
        return;

    }   
}

float Selection_parser::eval_numeric(AstNode_ptr& node, int at){    
    if(node->code == TOK_INT){
        return boost::get<int>(node->children[0]);
    } else if(node->code == TOK_FLOAT){
        return boost::get<float>(node->children[0]);
    } else if(node->code == TOK_X){
        return sys->traj[frame].coord[at](0);
    } else if(node->code == TOK_Y){
        return sys->traj[frame].coord[at](1);
    } else if(node->code == TOK_Z){        
        return sys->traj[frame].coord[at](2);
    } else if(node->code == TOK_BETA){
        return sys->atoms[at].beta;
    } else if(node->code == TOK_OCC){
        return sys->atoms[at].occupancy;
    } else if(node->code == TOK_UNARY_MINUS){
        return -eval_numeric(node->child_node(0),at);
    } else if(node->code == TOK_PLUS){
        return eval_numeric(node->child_node(0),at)
             + eval_numeric(node->child_node(1),at);
    } else if(node->code == TOK_MINUS){
        return eval_numeric(node->child_node(0),at)
             - eval_numeric(node->child_node(1),at);
    } else if(node->code == TOK_MULT){
        return eval_numeric(node->child_node(0),at)
             * eval_numeric(node->child_node(1),at);
    } else if(node->code == TOK_DIV){
        float v = eval_numeric(node->child_node(0),at);
        if(v==0.0) throw Pteros_error("Divition by zero in selection!");
        return eval_numeric(node->child_node(1),at) / v;

    } else if(node->code == TOK_POINT){
        // Extract point
        Eigen::Vector3f p;

        p(0) = eval_numeric(node->child_node(0),at);
        p(1) = eval_numeric(node->child_node(1),at);
        p(2) = eval_numeric(node->child_node(2),at);

        bool pbc = false;
        if(node->children.size()==4)
            pbc = node->child_as_bool(3);
        // Return distance
        if(pbc){
            return sys->Box(frame).distance(p, sys->traj[frame].coord[at]);
        } else {
            return (p - sys->traj[frame].coord[at]).norm();
        }

    } else if(node->code == TOK_VECTOR || node->code == TOK_PLANE ){
        // Extract point
        Eigen::Vector3f p;
        p(0) = eval_numeric(node->child_node(0),at);
        p(1) = eval_numeric(node->child_node(1),at);
        p(2) = eval_numeric(node->child_node(2),at);
        // Extract direction vector (or a normal if it's a plane)
        Eigen::Vector3f dir;
        dir(0) = eval_numeric(node->child_node(3),at);
        dir(1) = eval_numeric(node->child_node(4),at);
        dir(2) = eval_numeric(node->child_node(5),at);

        Eigen::Vector3f atom = sys->traj[frame].coord[at];

        // Get vector from p to current atom
        Eigen::Vector3f v = atom - p;

        // Project v onto dir
        v = (v.dot(dir)/dir.squaredNorm())*dir;

        if(node->code == TOK_PLANE){
            // Get closest point on a plane to atom
            v = atom-v;
        } else {
            // Get the end point of projection
            v += p;
        }

        bool pbc = false;
        if(node->children.size()==7)
            //TODO
            pbc = node->child_as_bool(6); // Is this correct???

        // Return distance between atom and v
        if(pbc){
            return sys->Box(frame).distance(atom, v);
        } else {
            return (atom-v).norm();
        }
    }
}
