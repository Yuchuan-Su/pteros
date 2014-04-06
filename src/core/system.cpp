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

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "pteros/core/system.h"
#include "pteros/core/selection.h"
#include "pteros/core/pteros_error.h"
#include "pteros/core/grid_search.h"
#include "pteros/core/format_recognition.h"
#include <boost/lexical_cast.hpp>
#include "pteros/core/mol_file.h"
#include <boost/bind.hpp>
// DSSP
#include "pteros_dssp_wrapper.h"


using namespace std;
using namespace pteros;
using namespace Eigen;

// Base constructor of the system class
System::System() {

}

// Construnt system from file
System::System(string fname) {    
    clear();
    load(fname);
}

System::System(const System& other){
    clear();
    atoms = other.atoms;
    traj = other.traj;
    force_field = other.force_field;
}

System& System::operator=(System other){
    clear();
    atoms = other.atoms;
    traj = other.traj;
    force_field = other.force_field;
    return *this;
}

// Clear the system (i.e. before reading new system from file)
void System::clear(){
    atoms.clear();
    traj.clear();
    force_field.clear();
}

void System::check_num_atoms_in_last_frame(){
    if(Frame_data(num_frames()-1).coord.size()!=num_atoms())
        throw Pteros_error("File contains "
                           +boost::lexical_cast<string>(Frame_data(num_frames()-1).coord.size())
                           +" atoms while the system has "
                           +boost::lexical_cast<string>(num_atoms())
                           );
}

// Load structure or trajectory
void System::load(string fname, int b, int e, int skip){
    // Create an IO file reader
    boost::shared_ptr<Mol_file> f = io_factory(fname,'r');

    int num_stored = 0;    
    // Do we have some structure?
    if(num_atoms()>0){
        // We have atoms already, so read only coordinates
        if(!f->get_content_type().coordinates && !f->get_content_type().trajectory)
            throw Pteros_error("File reader for file '"+fname
                               +"' is not capable of appending frames to the system!");

        // Check if we can read multiple coordinates
        if(f->get_content_type().trajectory){

            Mol_file_content c;
            c.trajectory = true;

            // Sanity check for frame range
            if((e<b && e!=-1)|| b<0)
                throw Pteros_error("Invalid frame range for reading!");

            int cur = 0; // This holds real frame index in trajectory

            // Skip frames if needed
            if(b>0){
                cout << "Skipping " << b << " frames..." << endl;
                Frame skip_fr;
                for(int i=0;i<b;++i){
                    f->read(NULL,&skip_fr,c);
                    cur++;
                }
            }

            int first = num_frames(); // Remember start

            cout << "Reading..."<<endl;

            int actually_read = 0;            

            while(true){
                // End frame reached?
                if(cur==e && e!=-1) break;

                // Append new frame where the data will go
                Frame fr;
                frame_append(fr);
                // Try to read into it
                bool ok = f->read(NULL,&Frame_data(num_frames()-1),c);
                if(!ok){
                    frame_delete(num_frames()-1); // Remove last frame - it's invalid
                    break;
                }

                check_num_atoms_in_last_frame();

                ++cur;
                ++actually_read;

                if(skip>0 && actually_read%skip!=0){
                    frame_delete(num_frames()-1); // Remove last frame - it's invalid
                    continue;
                } else {
                    actually_read = 0;
                }

                // If we are here new frame is already accepted
                ++num_stored;
            }        
        } else if(f->get_content_type().coordinates) {
            Mol_file_content c;
            c.coordinates = true;
            // File contains single frame
            // Append new frame where the data will go
            Frame fr;
            frame_append(fr);
            // Read it
            f->read(NULL,&Frame_data(num_frames()-1),c);
            check_num_atoms_in_last_frame();
            ++num_stored;
        }
    } else {
        // We don't have atoms yet, so we will read everything possible
        // Append new frame where the data will go
        Frame fr;
        frame_append(fr);
        Mol_file_content c = f->get_content_type();
        f->read(this,&Frame_data(num_frames()-1),c);

        check_num_atoms_in_last_frame();
        ++num_stored;

        assign_resindex();
    }

    cout << "Stored " << num_stored << " frames. Now " << num_frames() << " frames in the System" << endl;
}

// Destructor of the system class
System::~System() {}

void System::frame_dup(int fr){
    if(fr<0 || fr>=traj.size())
    	throw Pteros_error("Invalid frame for duplication!");
    traj.push_back(traj[fr]);
}

void System::frame_copy(int fr1, int fr2){
    if(fr1<0 || fr1>=traj.size() || fr2<0 || fr2>=traj.size())
    	throw Pteros_error("Invalid frame for copying!");
    traj[fr2] = traj[fr1];    
}

// Delete the range of frames. e = -1 is default
void System::frame_delete(int b, int e){
    int i;    

    if(e==-1) e = num_frames()-1;
    if(e<b || b<0 || e>num_frames()-1) throw Pteros_error("Invalid frame range for deletion");    

    // Get iterators for deletion
    vector<Frame>::iterator b_it, e_it;
    b_it = traj.begin();
    for(i=0;i<b;++i) b_it++;
    e_it = b_it;
    for(;i<e;++i) e_it++;
    e_it++; //Go one past the end

    traj.erase(b_it,e_it);

    // Check if there are some frames left. If not print the warning
    // that all selections are invalid!
    if(traj.size()==0) cout << "All frames are deleted. All selections are now INVALID!";
}

void System::frame_append(const Frame& fr){
    traj.push_back(fr);
}

void System::assign_resindex(){
    //cout << "Assigning resindex..." << endl;
    int curres = atoms[0].resid;
    int curchain = atoms[0].chain;
    int cur = 0;
    for(int i=0; i<atoms.size(); ++i){
        if( atoms[i].resid!=curres || atoms[i].chain!=curchain ){
            ++cur;
            curres = atoms[i].resid;
            curchain = atoms[i].chain;
        }
        atoms[i].resindex = cur;
    }
}

bool by_resindex_sorter(int i, int j, System& sys){
    return (sys.Atom_data(i).resindex < sys.Atom_data(j).resindex);
}

void System::sort_by_resindex()
{
    // Make and array of indexes to shuffle
    vector<int> ind(atoms.size());
    for(int i=0;i<ind.size();++i) ind[i] = i;
    // Sort indexes
    sort(ind.begin(),ind.end(),boost::bind(&by_resindex_sorter,_1,_2,*this));
    // Now shuffle atoms and coordinates according to indexes
    vector<Atom> tmp(atoms); //temporary
    for(int i=0;i<ind.size();++i) atoms[i] = tmp[ind[i]];

    std::vector<Eigen::Vector3f> tmp_coord;
    for(int j=0; j<traj.size(); ++j){ // Over all frames
        tmp_coord = traj[j].coord; //temporary
        for(int i=0;i<ind.size();++i) traj[j].coord[i] = tmp_coord[ind[i]];
    }
}

void System::atoms_dup(const vector<int>& ind, Selection* res_sel){
    // Sanity check
    if(!ind.size()) throw Pteros_error("No atoms to duplicate!");
    for(int i=0; i<ind.size(); ++i){
        if(ind[i]<0 || ind[i]>atoms.size()-1)
            throw Pteros_error("Invalid index for atom duplication!");
    }

    // Duplicate atoms
    int first_added = atoms.size();
    int last_added = atoms.size()+ind.size()-1;
    // Prepare by increasing capacity of vectors
    atoms.reserve(atoms.size()+ind.size());
    for(int j=0; j<traj.size(); ++j){
        traj[j].coord.reserve(atoms.size()+ind.size());
    }

    // Now add atoms
    for(int i=0; i<ind.size(); ++i){
        // Add new atom
        atoms.push_back(atoms[ind[i]]);
        // Add new coordinate slot
        for(int j=0; j<traj.size(); ++j){
            traj[j].coord.push_back(traj[j].coord[ind[i]]);
        }
    }

    if(res_sel) res_sel->modify(*this,first_added,last_added);
}

void System::atoms_add(const vector<Atom>& atm, const vector<Vector3f>& crd, Selection* res_sel){
    // Sanity check
    if(!atm.size()) throw Pteros_error("No atoms to add!");
    if(atm.size()!=crd.size()) throw Pteros_error("Wrong number of coordinates for adding atoms!");

    int first_added = atoms.size();
    int last_added = atoms.size()+atm.size()-1;
    // Prepare by increasing capacity of vectors
    atoms.reserve(atoms.size()+atm.size());
    for(int j=0; j<traj.size(); ++j){
        traj[j].coord.reserve(atoms.size()+atm.size());
    }
    // Now add atoms
    for(int i=0; i<atm.size(); ++i){
        // Add new atom
        atoms.push_back(atm[i]);
        // Add new coordinate slot
        for(int j=0; j<traj.size(); ++j){
            traj[j].coord.push_back(crd[i]);
        }
    }

    if(res_sel) res_sel->modify(*this,first_added,last_added);
}

void System::atoms_delete(const std::vector<int> &ind){
    int i,fr;

    // Sanity check
    if(!ind.size()) throw Pteros_error("No atoms to delete!");
    for(int i=0; i<ind.size(); ++i){
        if(ind[i]<0 || ind[i]>atoms.size()-1)
            throw Pteros_error("Invalid index for atom deletion!");
    }

    // Mark atoms for deletion by assigning negative mass
    for(i=0;i<ind.size();++i)
        atoms[ind[i]].mass = -1.0;

    // Cycle over all atoms and keep only those with positive mass
    vector<pteros::Atom> tmp = atoms;
    atoms.clear();
    for(i=0;i<tmp.size();++i){
        if(tmp[i].mass>=0) atoms.push_back(tmp[i]);
    }

    // Now cycle over trajectory and keep only corresponding coordinates
    vector<Vector3f> tmp_coord;
    for(fr=0; fr<num_frames(); ++fr){
        // Make a copy of traj coords
        tmp_coord = traj[fr].coord;
        traj[fr].coord.clear();
        for(i=0;i<tmp.size();++i){
            if(tmp[i].mass>=0) traj[fr].coord.push_back(tmp_coord[i]);
        }
    }
    // Reassign residue indexes
    //system->assign_resindex();
    //system->update_selections();
}

void System::append(const System &sys){
    //Sanity check
    if(num_frames()!=sys.num_frames()) throw Pteros_error("Can't merge systems with different number of frames!");
    // Merge atoms
    copy(sys.atoms.begin(),sys.atoms.end(),back_inserter(atoms));
    // Merge coordinates
    for(int fr=0; fr<num_frames(); ++fr)
        copy(sys.traj[fr].coord.begin(),sys.traj[fr].coord.end(),back_inserter(traj[fr].coord));
    // Reassign resindex
    assign_resindex();
}

void System::append(const Selection &sel)
{
    //Sanity check
    if(num_frames()!=sel.get_system()->num_frames()) throw Pteros_error("Can't merge systems with different number of frames!");
    // Merge atoms
    atoms.reserve(atoms.size()+sel.size());
    for(int i=0;i<sel.size();++i) atoms.push_back(sel.Atom(i));
    // Merge coordinates
    for(int fr=0; fr<num_frames(); ++fr){
        traj[fr].coord.reserve(atoms.size()+sel.size());
        for(int i=0;i<sel.size();++i) traj[fr].coord.push_back(sel.XYZ(i,fr));
    }
    // Reassign resindex
    assign_resindex();
}

inline void wrap_coord(Vector3f& point, const Matrix3f& box,
                       const Vector3i dims_to_wrap = Vector3i::Ones()){
    Matrix3f b;
    b.col(0) = box.col(0).normalized();
    b.col(1) = box.col(1).normalized();
    b.col(2) = box.col(2).normalized();

    int i;
    float intp,fracp;
    // Get scalar projections onto box basis vectors
    Vector3f prj;    
    Vector3f box_dim = box.colwise().norm();

    prj = b.inverse()*point;

    for(i=0;i<3;++i){
        if(dims_to_wrap(i)!=0){
            fracp = std::modf(prj(i)/box_dim(i),&intp);
            if(fracp<0) fracp = fracp+1;
            prj(i) = box_dim(i)*fracp;
        }
    }   

    // Get back to lab coordinates
    point = b*prj;
}

float System::distance(int i, int j, int fr, bool is_periodic, Vector3i_const_ref dims) const {
    if(is_periodic){
        return traj[fr].box.distance(traj[fr].coord[i], traj[fr].coord[j], true, dims);
    } else {
        return (traj[fr].coord[i] - traj[fr].coord[j]).norm();
    }
}

void System::wrap_all(int fr, Vector3i_const_ref dims_to_wrap){
    for(int i=0;i<num_atoms();++i){
        traj[fr].box.wrap_point(XYZ(i,fr),dims_to_wrap);
    }
}

inline float LJ_en_kernel(float C6, float C12, float r){
    float tmp = 1/r;
    tmp = tmp*tmp; // (1/r)^2
    tmp = tmp*tmp*tmp; // (1/r)^6
    return C12*tmp*tmp-C6*tmp;
}

#define ONE_4PI_EPS0      138.935456

inline float Coulomb_en_kernel(float q1, float q2, float r){
    return ONE_4PI_EPS0*q1*q2/r;
}

string Energy_components::to_str(){
    return    boost::lexical_cast<string>(total) + " "
            + boost::lexical_cast<string>(lj_sr) + " "
            + boost::lexical_cast<string>(lj_14) + " "
            + boost::lexical_cast<string>(q_sr) + " "
            + boost::lexical_cast<string>(q_14);
}

void System::add_non_bond_energy(Energy_components &e, int a1, int a2, int frame, bool is_periodic) const
{
    // First check if this pair is not in exclusions
    if( force_field.exclusions[a1].count(a2) == 0 ){
        // Required at1 < at2
        int at1,at2;
        if(a1<a2){
            at1 = a1;
            at2 = a2;
        } else {
            at1 = a2;
            at2 = a1;
        }

        float e1,e2;

        int N = force_field.LJ14_interactions.size();
        //float r = distance(XYZ(at1,frame),XYZ(at2,frame),frame,is_periodic);
        float r = distance(at1,at2,frame);

        // Check if this is 1-4 pair
        boost::unordered_map<int,int>::iterator it = const_cast<System&>(*this).force_field.LJ14_pairs.find(at1*N+at2);
        if( it == force_field.LJ14_pairs.end() ){
            // Normal, not 1-4
            e1 = LJ_en_kernel(force_field.LJ_C6(atoms[at1].type,atoms[at2].type),
                                   force_field.LJ_C12(atoms[at1].type,atoms[at2].type),
                                   r);
            e2 = Coulomb_en_kernel(atoms[at1].charge,
                                       atoms[at2].charge,
                                       r);
            e.lj_sr += e1;
            e.q_sr += e2;
            e.total += (e1 + e2);
        } else {
            // 1-4
            e1 = LJ_en_kernel(force_field.LJ14_interactions[it->second](0),
                                   force_field.LJ14_interactions[it->second](1),
                                   r);
            e2 = Coulomb_en_kernel(atoms[at1].charge,
                                       atoms[at2].charge,
                                       r)
                    * force_field.fudgeQQ;

            e.lj_14 = e1;
            e.q_14 = e2;
            e.total += (e1 + e2);
        }
    }
}

Energy_components System::non_bond_energy(const std::vector<Eigen::Vector2i> &nlist, int fr, bool is_periodic) const
{
    Energy_components e;
    int n = nlist.size();

    for(int i=0;i<n;++i){
        add_non_bond_energy(e,nlist[i](0),nlist[i](1),fr,is_periodic);
    }

    return e;
}

#ifndef NO_CPP11

void System::dssp(string fname) const {
    ofstream f(fname.c_str());
    Selection sel(const_cast<System&>(*this),"all");
    dssp_wrapper(sel,f);
    f.close();
}

string System::dssp() const{
    Selection sel(const_cast<System&>(*this),"all");
    return dssp_string(sel);
}

#endif
