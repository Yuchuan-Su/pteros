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

#include "pteros/core/system.h"
#include "pteros/core/pteros_error.h"
#include "pteros/core/grid_search.h"
#include "pteros/core/force_field.h"
#include <cmath>
#include <functional>

using namespace std;
using namespace pteros;
using namespace Eigen;

float shift_const_A(int alpha, float r1, float rc){
    return -(( (alpha+4)*rc - (alpha+1)*r1 )/( pow(rc,alpha+2)*pow(rc-r1,2) ));
}

float shift_const_B(int alpha, float r1, float rc){
    return ( (alpha+3)*rc - (alpha+1)*r1 )/( pow(rc,alpha+2)*pow(rc-r1,3) );
}

float shift_const_C(int alpha, float r1, float rc, float A, float B){
    return 1.0/pow(rc,alpha) - (A/3.0)*pow(rc-r1,3) - (B/4.0)*pow(rc-r1,4);
}

// Plain LJ kernel
float Force_field::LJ_en_kernel(float C6, float C12, float r){
    float r_inv = 1.0/r;
    float tmp = r_inv*r_inv; // (1/r)^2
    tmp = tmp*tmp*tmp; // (1/r)^6
    return C12*tmp*tmp-C6*tmp;
}

// Shifted LJ kernel
float Force_field::LJ_en_kernel_shifted(float C6, float C12, float r){
    float val12 =  pow(r,-12)
            -(shift_A_12/3.0)*pow(r-rvdw_switch,3)
            -(shift_B_12/4.0)*pow(r-rvdw_switch,4)
            -shift_C_12;
    float val6 =  pow(r,-6)
            -(shift_A_6/3.0)*pow(r-rvdw_switch,3)
            -(shift_B_6/4.0)*pow(r-rvdw_switch,4)
            -shift_C_6;
    return C12*val12 - C6*val6;
}


#define ONE_4PI_EPS0      138.935456

// Plane Coulomb kernel
inline float Force_field::Coulomb_en_kernel(float q1, float q2, float r){
    return coulomb_prefactor*q1*q2/r;
}

// Reaction field Coulomb kernel
inline float Force_field::Coulomb_en_kernel_rf(float q1, float q2, float r){
    return coulomb_prefactor*q1*q2*(1.0/r + k_rf*r*r - c_rf);
}

// Shifted Coulomb kernel
inline float Force_field::Coulomb_en_kernel_shifted(float q1, float q2, float r){
    return coulomb_prefactor*q1*q2*( 1.0/r
                                     -(shift_A_1/3.0)*pow(r-rcoulomb_switch,3)
                                     -(shift_B_1/4.0)*pow(r-rcoulomb_switch,4)
                                     -shift_C_1
                                     );
}


void Force_field::setup_kernels(){
    using namespace placeholders;

    // Set Coulomb prefactor
    coulomb_prefactor = ONE_4PI_EPS0 / epsilon_r;

    if(coulomb_type=="reaction-field"){
        // In case of reaction field precompute constanst
        if(epsilon_rf){
            k_rf = (1.0/(rcoulomb*rcoulomb*rcoulomb))
                    * (epsilon_rf-epsilon_r) / (2.0*epsilon_rf+epsilon_r);
        } else {
            // for epsilon_rf = 0 (which means inf)
            k_rf = 0.5/(rcoulomb*rcoulomb*rcoulomb);
        }
        c_rf = (1.0/rcoulomb) + k_rf*rcoulomb*rcoulomb;

        // Set coulomb kernel pointer
        coulomb_kernel_ptr = bind(&Force_field::Coulomb_en_kernel_rf,this,_1,_2,_3);
        cout << "\tCoulomb kernel: reaction_field" << endl;
    } else if(coulomb_modifier=="potential-shift") {
        // Compute shift constants for power 1
        shift_A_1 = shift_const_A(1,rcoulomb_switch,rcoulomb);
        shift_B_1 = shift_const_B(1,rcoulomb_switch,rcoulomb);
        shift_C_1 = shift_const_C(1,rcoulomb_switch,rcoulomb,shift_A_1,shift_B_1);

        coulomb_kernel_ptr = bind(&Force_field::Coulomb_en_kernel_shifted,this,_1,_2,_3);
        cout << "\tCoulomb kernel: shifted" << endl;
    } else {
        // In other cases set plain Coulomb interaction
        coulomb_kernel_ptr = bind(&Force_field::Coulomb_en_kernel,this,_1,_2,_3);
        cout << "\tCoulomb kernel: plain cutoff" << endl;
    }

    if(vdw_modifier == "potential-shift"){
        // Compute shift constants for powers 6 and 12
        shift_A_6 = shift_const_A(6,rvdw_switch,rvdw);
        shift_A_12 = shift_const_A(12,rvdw_switch,rvdw);
        shift_B_6 = shift_const_B(6,rvdw_switch,rvdw);
        shift_B_12 = shift_const_B(12,rvdw_switch,rvdw);
        shift_C_6 = shift_const_C(6,rvdw_switch,rvdw,shift_A_6,shift_B_6);
        shift_C_12 = shift_const_C(12,rvdw_switch,rvdw,shift_A_12,shift_B_12);

        LJ_kernel_ptr = bind(&Force_field::LJ_en_kernel_shifted,this,_1,_2,_3);
        cout << "\tLJ kernel: shifted" << endl;
    } else {
        LJ_kernel_ptr = bind(&Force_field::LJ_en_kernel,this,_1,_2,_3);
        cout << "\tLJ kernel: plain cutoff" << endl;
    }
}

Force_field::Force_field():  ready(false) {}

Force_field::Force_field(const Force_field &other){
    charge_groups = other.charge_groups;
    exclusions = other.exclusions;
    LJ_C6 = other.LJ_C6;
    LJ_C12 = other.LJ_C12;
    LJ14_interactions = other.LJ14_interactions;
    LJ14_pairs = other.LJ14_pairs;
    fudgeQQ = other.fudgeQQ;

    ready = other.ready;
}

Force_field &Force_field::operator=(Force_field other){
    charge_groups = other.charge_groups;
    exclusions = other.exclusions;
    LJ_C6 = other.LJ_C6;
    LJ_C12 = other.LJ_C12;
    LJ14_interactions = other.LJ14_interactions;
    LJ14_pairs = other.LJ14_pairs;
    fudgeQQ = other.fudgeQQ;
    ready = other.ready;

    return *this;
}

void Force_field::clear(){
    charge_groups.clear();
    exclusions.clear();
    LJ_C6.fill(0.0);
    LJ_C12.fill(0.0);
    LJ14_interactions.clear();
    LJ14_pairs.clear();
    fudgeQQ = 0.0;

    ready = false;
}
