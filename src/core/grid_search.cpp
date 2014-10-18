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

#include "pteros/core/grid_search.h"
#include "pteros/core/selection.h"
#include "pteros/core/pteros_error.h"
#include <vector>
#include <map>
#include <algorithm>
#include "time.h"
#include <iostream>
#include <thread>
#include <array>

using namespace std;
using namespace pteros;
using namespace Eigen;

// Get intersection of two 1d bars
void overlap_1d(float a1, float a2, float b1, float b2, float& res1, float& res2){
    res1 = res2 = 0.0;
    if(a1<b1){
        if(a2<b1){
            return; // No overlap
         } else { //a2>b1
            res1 = b1;
            if(a2<b2) res2=a2; else res2=b2;
        }
    } else { //a1>b1
        if(a1>b2){
            return; //No overlap
        } else { //a1<b2
            res1 = a1;
            if(a2>b2) res2=b2; else res2=a2;
        }
    }
}


Grid_searcher::Grid_searcher(float d, const Selection &sel,
                            std::vector<Eigen::Vector2i>& bon,
                            bool absolute_index,
                            bool periodic,
                            std::vector<float>* dist_vec){
    cutoff = d;
    is_periodic = periodic;    
    abs_index = absolute_index;
    box = sel.get_system()->Box(sel.get_frame());

    create_grid(grid1,sel);
    populate_grid(grid1,sel);
    do_search(sel,bon,dist_vec);
}

Grid_searcher::Grid_searcher(float d, const Selection &sel1, const Selection &sel2,
                            std::vector<Eigen::Vector2i>& bon,
                            bool absolute_index,
                            bool periodic,
                            std::vector<float>* dist_vec){
    cutoff = d;
    is_periodic = periodic;    
    abs_index = absolute_index;
    box = sel1.get_system()->Box(sel1.get_frame());

    create_grid2(sel1,sel2);
    populate_grid(grid1,sel1);
    populate_grid(grid2,sel2);
    do_search(sel1,sel2,bon,dist_vec);
}

Grid_searcher::Grid_searcher(){
}


void Grid_searcher::assign_to_grid(float d, const Selection &sel,
                    bool absolute_index,
                    bool periodic){
    cutoff = d;
    is_periodic = periodic;
    abs_index = absolute_index;
    box = sel.get_system()->Box(sel.get_frame());

    create_grid(grid1,sel);
    populate_grid(grid1,sel);
    // Remember pointer to this selection
    p_sel = const_cast<Selection*>(&sel);
}

void Grid_searcher::create_custom_grid(int nX, int nY, int nZ){
    NgridX = nX;
    NgridY = nY;
    NgridZ = nZ;

    grid1.resize( boost::extents[NgridX][NgridY][NgridZ] );
}

void Grid_searcher::fill_custom_grid(const Selection sel, bool absolute_index){

    box = sel.get_system()->Box(sel.get_frame());

    if(box.is_triclinic()){
        throw Pteros_error("Custom grids are not implemented for triclinic boxes");
    };

    min.fill(0.0);
    max = box.extents();

    is_periodic = true;
    abs_index = absolute_index;

    populate_grid(grid1,sel);
}

vector<int>& Grid_searcher::cell_of_custom_grid(int x, int y, int z){
    return grid1[x][y][z];
}

void Grid_searcher::search_within(Vector3f_const_ref coord, vector<int> &bon){
    int n1,n2,n3,i,m1,m2,m3;

    bon.clear();
    Vector3f coor(coord);

    // Get coordinates in triclinic basis if needed
    if(is_periodic && box.is_triclinic()) coor = box.lab_to_box(coor);

    // Assign point to grid
    n1 = floor((NgridX-1)*(coor(0)-min(0))/(max(0)-min(0)));
    n2 = floor((NgridY-1)*(coor(1)-min(1))/(max(1)-min(1)));
    n3 = floor((NgridZ-1)*(coor(2)-min(2))/(max(2)-min(2)));

    if(is_periodic){
        // If periodic and extends over the grid dimensions wrap it
        while(n1>=NgridX || n1<0)
            n1>=0 ? n1 %= NgridX : n1 = NgridX + n1%NgridX;
        while(n2>=NgridY || n2<0)
            n2>=0 ? n2 %= NgridY : n2 = NgridY + n2%NgridY;
        while(n3>=NgridZ || n3<0)
            n3>=0 ? n3 %= NgridZ : n3 = NgridZ + n3%NgridZ;
    } else {
        // In non-periodic variant discard point if doesn't fit into bounding box
        if(n1<0 || n1>=NgridX || n2<0 || n2>=NgridY || n3<0 || n3>=NgridZ) return;
    }

    float d;

    // Get neighbour list
    get_nlist(n1,n2,n3);
    // Add central cell to the list
    nlist.push_back(Vector3i(n1,n2,n3));
    int nlist_size = nlist.size();
    // Searh in all cells
    for(i=0;i<nlist_size;++i){
        m1 = nlist[i](0);
        m2 = nlist[i](1);
        m3 = nlist[i](2);
        int n = grid1[m1][m2][m3].size();
        for(int c=0;c<n;++c){            
            if(!is_periodic)
                d = (p_sel->XYZ(grid1[m1][m2][m3][c]) - coor).norm();
            else
                d = box.distance(p_sel->XYZ(grid1[m1][m2][m3][c]),coor);

            if(d<=cutoff){
                if(abs_index){
                    bon.push_back( p_sel->Index(grid1[m1][m2][m3][c]) );
                } else {
                    bon.push_back( grid1[m1][m2][m3][c] );
                }                
            }            

        }
    }
}


void Grid_searcher::search_within(const Selection &target, std::vector<int> &bon, bool include_self){
    bon.clear();  

    int i,j,k,c,n1,n2,nlist_size,m1,m2,m3,N1,N2;
    float d;
    Vector3f coor1,coor2;

    // Allocate grid2 and populate it from target
    grid2.resize( boost::extents[NgridX][NgridY][NgridZ] );
    populate_grid(grid2,target);

    //cout << "Grid dimensions: " << NgridX << " " << NgridY << " " << NgridZ << endl;

    // Cycle over all cells of grid2
    for(i=0;i<NgridX;++i){
        for(j=0;j<NgridY;++j){
            for(k=0;k<NgridZ;++k){
                // Get number of atoms in current grid2 cell
                N2 = grid2[i][j][k].size();
                // If no atoms than just skip this cell
                if(N2==0) continue;
                // Get neighbour list
                get_nlist(i,j,k);
                // Add central cell to the list                
                nlist.push_back(Vector3i(i,j,k));

                nlist_size = nlist.size();
                // Cycle over neighbouring cells
                //cout << endl;
                for(c=0;c<nlist_size;++c){

                    m1 = nlist[c](0);
                    m2 = nlist[c](1);
                    m3 = nlist[c](2);

                    // Get number of atoms in neighbour grid1 cell
                    N1 = grid1[m1][m2][m3].size();
                    // Skip empty pairs
                    if(N1==0) continue;

                    // Cycle over N2 and N1                    
                    for(n2=0;n2<N2;++n2){
                        coor1 = target.XYZ(grid2[i][j][k][n2]);

                        for(n1=0;n1<N1;++n1){
                            // Skip already used points
                            if(grid1[m1][m2][m3][n1]<0) continue;

                            coor2 = p_sel->XYZ(grid1[m1][m2][m3][n1]);

                            if(!is_periodic)
                                d = (coor2 - coor1).norm();
                            else
                                d = box.distance(coor2,coor1);

                            if(d<=cutoff){
                                if(abs_index){
                                    bon.push_back( p_sel->Index(grid1[m1][m2][m3][n1]) );
                                } else {
                                    bon.push_back( grid1[m1][m2][m3][n1] );
                                }
                                // Mark atom in grid1 as already added
                                grid1[m1][m2][m3][n1] = -grid1[m1][m2][m3][n1];                                
                            }
                        }
                    }

                }
            }
        }
    }

    // Restore grid1 for possible later searches
    for(i=0;i<NgridX;++i)
        for(j=0;j<NgridY;++j)
            for(k=0;k<NgridZ;++k)
                for(n1=0;n1<grid1[i][j][k].size();++n1)
                    grid1[i][j][k][n1] = abs(grid1[i][j][k][n1]);

    if(include_self){
        // Add all target atoms to result
        copy(target.index_begin(),target.index_end(),back_inserter(bon));
    }

    sort(bon.begin(),bon.end());
    // Remove duplicates
    vector<int>::iterator it = std::unique(bon.begin(), bon.end());
    // Get rid of the tail with garbage
    bon.resize( it - bon.begin() );

    if(!include_self){
        vector<int> dum = bon;
        bon.clear();
        set_difference(dum.begin(),dum.end(),target.index_begin(),target.index_end(),back_inserter(bon));
    }
}



// Search over part of space. To be called in a thread.
void Grid_searcher::do_part_within(int dim, int _b, int _e,
                             const Selection &src,    //grid1
                             const Selection &target, //grid2
                             std::vector<atomwrapper<bool>>& used
                             ){

    Vector3i b(0,0,0);
    Vector3i e(NgridX,NgridY,NgridZ);
    int dim_max = e(dim);
    b(dim)= _b;
    e(dim)= _e;
    int i,j,k,i1,nlist_size,N1,N2,m1,m2,m3,c,n1,n2,ind;
    float d;

    vector<Vector3i> nlist; // Local nlist

    float cutoff2 = cutoff*cutoff;

    for(i=b(0);i<e(0);++i){
        for(j=b(1);j<e(1);++j){
            for(k=b(2);k<e(2);++k){

                const vector<int>& vt = grid2[i][j][k];

                // Get number of atoms in current target cell
                N2 = vt.size();
                // If no atoms than just skip this cell
                if(N2==0) continue;

                // Matrix of pre-computed coordinates for target points in this cell
                // Saves access to grid and computing XYZ in place. Makes a big
                // difference for large cutoffs!                
                MatrixXf pre(3,N2);
                for(n2=0;n2<N2;++n2){ //over target atoms
                    pre.col(n2) = target.XYZ(vt[n2]);
                }                

                // Get neighbour list
                get_nlist_local(i,j,k,nlist);
                // Add central cell to the list
                nlist.push_back(Vector3i(i,j,k));

                // Cycle over neighbouring cells
                for(c=0;c<nlist.size();++c){

                    const vector<int>& vs = grid1[nlist[c](0)][nlist[c](1)][nlist[c](2)];

                    // Get number of atoms in neighbour grid1 cell
                    N1 = vs.size();

                    // Skip empty cells
                    if(N1==0) continue;

                    // Cycle over N1
                    for(n1=0;n1<N1;++n1){ // Over source atoms

                        ind = vs[n1];
                        // Skip already used source points
                        if(used[ind].load()) continue;

                        const Vector3f coor = src.XYZ(ind); // Coord of source point

                        if(is_periodic){
                            for(n2=0;n2<N2;++n2){ //over target atoms of current cell
                                d = box.distance_squared(pre.col(n2), coor);
                                if(d<=cutoff2){
                                    used[ind].store(true);
                                    break;
                                }
                            }
                        } else {
                            for(n2=0;n2<N2;++n2){ //over target atoms of current cell
                                d = (pre.col(n2) - coor).squaredNorm();
                                if(d<=cutoff2){
                                    used[ind].store(true);
                                    break;
                                }
                            }
                        }

                    }

                    //--
                }


            }
        }
    }
}


void search_in_pair_of_cells(int sx, int sy, int sz, // src cell
                             int tx, int ty, int tz, // target cell                             
                             const boost::multi_array<std::vector<Grid_element>,3>& grid1,
                             const boost::multi_array<std::vector<Grid_element>,3>& grid2,
                             std::vector<atomwrapper<bool>>& used,
                             const Periodic_box& box,
                             float cutoff2, bool is_periodic)
{
    int Ns,Nt,ind,s,t;    
    float d;

    Ns = grid1[sx][sy][sz].size(); //src
    Nt = grid2[tx][ty][tz].size(); //target

    if(Ns*Nt==0) return; // Nothing to do

    const vector<Grid_element>& sv = grid1[sx][sy][sz];
    const vector<Grid_element>& tv = grid2[tx][ty][tz];

    for(s=0;s<Ns;++s){
        ind = sv[s].index;
        // Skip already used source points
        if(used[ind].load()) continue;

        Vector3f* p = sv[s].coor_ptr; // Coord of source point

        if(is_periodic){
            for(t=0;t<Nt;++t){
                d = box.distance_squared(*(tv[t].coor_ptr),*p);
                if(d<=cutoff2){
                    used[ind].store(true);
                    break;
                }
            }
        } else {
            for(t=0;t<Nt;++t){
                d = (*(tv[t].coor_ptr)-*p).squaredNorm();
                if(d<=cutoff2){
                    used[ind].store(true);
                    break;
                }
            }
        }

    }
}



void Grid_searcher::do_part_within_fast(int dim, int _b, int _e,
                             const Selection &src,    //grid1
                             const Selection &target, //grid2
                             std::vector<atomwrapper<bool>>& used
                             ){
    Vector3i b(0,0,0);
    Vector3i e(NgridX,NgridY,NgridZ);
    int dim_max = e(dim);
    b(dim)= _b;
    e(dim)= _e;

    int i,j,k,c,sx,sy,sz,t,ind;
    vector<Vector3i> nlist; // Local nlist
    nlist.reserve(27);

    float cutoff2 = cutoff*cutoff;

    for(i=b(0);i<e(0);++i){
        for(j=b(1);j<e(1);++j){
            for(k=b(2);k<e(2);++k){

                // Search in central cell
                search_in_pair_of_cells(i,j,k, //src cell
                                        i,j,k, //target cell
                                        grid_coor1, grid_coor2,
                                        used, box, cutoff2, is_periodic);
                // Get nlist                
                get_nlist_local(i,j,k,nlist);

                // Cycle over nlist
                for(c=0;c<nlist.size();++c){
                    sx = nlist[c](0);
                    sy = nlist[c](1);
                    sz = nlist[c](2);

                    search_in_pair_of_cells(i,j,k, //src cell
                                            sx,sy,sz, //target cell                                            
                                            grid_coor1, grid_coor2,
                                            used, box, cutoff2, is_periodic);
                }

            }
        }
    }

}

// Search is around target, atoms from src are returned
Grid_searcher::Grid_searcher(float d,
                            const Selection &src,
                            const Selection &target,
                            std::vector<int>& bon,
                            bool include_self,
                            bool absolute_index,
                            bool periodic){

    cutoff = d;
    is_periodic = periodic;
    abs_index = absolute_index;

    // Get current box
    box = src.get_system()->Box(src.get_frame());

    //------------
    // Grid creation part
    //------------        

    // Determine bounding box
    if(!is_periodic){
        // Get the minmax of each selection
        Vector3f min1,min2,max1,max2;

        src.minmax(min1,max1);
        target.minmax(min2,max2);
        // Add a "halo: of size cutoff for each of them
        min1.array() -= cutoff;
        max1.array() += cutoff;
        min2.array() -= cutoff;
        max2.array() += cutoff;

        // Find true bounding box
        for(int i=0;i<3;++i){
            overlap_1d(min1(i),max1(i),min2(i),max2(i),min(i),max(i));
            // If no overlap just exit
            if(max(i)==min(i)) return;
        }
    } else {
        // Set dimensions of the current unit cell
        min.fill(0.0);
        max = box.extents();
    }

    set_grid_size(min,max, std::max(src.size(),target.size()),box);

    // Allocate both grids
    grid_coor1.resize( boost::extents[NgridX][NgridY][NgridZ] );
    grid_coor2.resize( boost::extents[NgridX][NgridY][NgridZ] );

    // Fill grids
    populate_coor_grid(grid_coor1,src);
    populate_coor_grid(grid_coor2,target);

    // Array of atomic bools for used source points
    std::vector<atomwrapper<bool>> used(src.size());
    for(int i=0;i<used.size();++i) used[i].store(false);

    //------------
    // Search part
    //------------
    bon.clear();    

    Vector3f coor1;

    // See if we need parallelization
    int max_N, max_dim;
    Vector3i dims(NgridX,NgridY,NgridZ);
    max_N = dims.maxCoeff(&max_dim);

    int nt = std::min(max_N, int(std::thread::hardware_concurrency()));

    if(nt>1){
        // Parallel search

        // Determine parts for each thread
        vector<int> b(nt),e(nt);
        int cur=0;
        for(int i=0;i<nt-1;++i){
            b[i]=cur;
            cur += dims(max_dim)/nt;
            e[i]=cur;
        }
        b[nt-1]=cur;
        e[nt-1]=dims(max_dim);

        //for(int i=0;i<nt;++i) cout << b[i] << ":" << e[i] << " ";
        //cout << endl;

        // Launch threads
        vector<thread> threads;
        for(int i=0;i<nt;++i){
            threads.push_back( thread(
                                   std::bind(
                                       &Grid_searcher::do_part_within_fast,
                                       //&Grid_searcher::do_part_within,
                                       this,
                                       max_dim,
                                       b[i],
                                       e[i],
                                       ref(src),
                                       ref(target),
                                       ref(used)
                                   )
                                )
                             );
        }

        // Wait for threads
        for(auto& t: threads) t.join();

    } else {
        // Serial search, no need to launch separate thread
        do_part_within_fast(max_dim,0,dims(max_dim),src,target,used);
    }


    // Convert used array to indexes
    if(abs_index){
        for(int i=0;i<used.size();++i)
            if(used[i].load()) bon.push_back(src.Index(i));
    } else {
        for(int i=0;i<used.size();++i)
            if(used[i].load()) bon.push_back(i);
    }

    if(include_self){
        // Add all target atoms to result
        copy(target.index_begin(),target.index_end(),back_inserter(bon));
    }

    sort(bon.begin(),bon.end());

    // Shoud be no duplicates without include_self!
    if(include_self){
        // Remove duplicates
        vector<int>::iterator it = std::unique(bon.begin(), bon.end());
        // Get rid of the tail with garbage
        bon.resize( it - bon.begin() );
    }

    if(!include_self){
        vector<int> dum = bon;
        bon.clear();
        set_difference(dum.begin(),dum.end(),target.index_begin(),target.index_end(),back_inserter(bon));
    }

}


void Grid_searcher::set_grid_size(const Vector3f& min, const Vector3f& max,
                                  int Natoms, const Periodic_box& box){

    /*  Our grids should satisfy these equations:
        NgridX * NgridY * NgridZ = Natoms
        NgridX/NgridY = a/b
        NgridY/NgridZ = b/c
        NgridX/NgridZ = a/c
        This lead to the following:
    */

    NgridX = floor(std::pow(double(Natoms*(max(0)-min(0))*(max(0)-min(0))/
                ((max(1)-min(1))*(max(2)-min(2)))), double(1.0/3.0))) ;
    NgridY = floor(std::pow(double(Natoms*(max(1)-min(1))*(max(1)-min(1))/
                ((max(0)-min(0))*(max(2)-min(2)))), double(1.0/3.0))) ;
    NgridZ = floor(std::pow(double(Natoms*(max(2)-min(2))*(max(2)-min(2))/
                ((max(0)-min(0))*(max(1)-min(1)))), double(1.0/3.0))) ;

    if(NgridX==0) NgridX = 1;
    if(NgridY==0) NgridY = 1;
    if(NgridZ==0) NgridZ = 1;    

    // Real grid vectors:
    float dX = (max(0)-min(0))/NgridX;
    float dY = (max(1)-min(1))/NgridY;
    float dZ = (max(2)-min(2))/NgridZ;

    // See if some of lab extents smaller than cutoff
    /*
    if(dX<cutoff) NgridX = floor(extX/cutoff);
    if(dY<cutoff) NgridY = floor(extY/cutoff);
    if(dZ<cutoff) NgridZ = floor(extZ/cutoff);
    */

    // See if some of grid vectors projected to lab axes smaller than cutoff
    while(box.box_to_lab(Vector3f(dX,0.0,0.0))(0) < cutoff && NgridX>1){
        --NgridX;
        dX = (max(0)-min(0))/NgridX;
    }
    while(box.box_to_lab(Vector3f(0.0,dY,0.0))(1) < cutoff && NgridY>1){
        --NgridY;
        dY = (max(1)-min(1))/NgridY;
    }
    while(box.box_to_lab(Vector3f(0.0,0.0,dZ))(2) < cutoff && NgridZ>1){
        --NgridZ;
        dZ = (max(2)-min(2))/NgridZ;
    }

    // See if some of lab extents larger than 2*cutoff
    /*
    if(dX>2.0*cutoff) NgridX = floor(extX/(2.0*cutoff));
    if(dY>2.0*cutoff) NgridY = floor(extY/(2.0*cutoff));
    if(dZ>2.0*cutoff) NgridZ = floor(extZ/(2.0*cutoff));
    */

    // See if some of grid vectors projected to lab axes larger than 2*cutoff
    while(box.box_to_lab(Vector3f(dX,0.0,0.0))(0) > 2.0*cutoff){
        ++NgridX;
        dX = (max(0)-min(0))/NgridX;
    }
    while(box.box_to_lab(Vector3f(0.0,dY,0.0))(1) > 2.0*cutoff){
        ++NgridY;
        dY = (max(1)-min(1))/NgridY;
    }
    while(box.box_to_lab(Vector3f(0.0,0.0,dZ))(2) > 2.0*cutoff){
        ++NgridZ;
        dZ = (max(2)-min(2))/NgridZ;
    }

}

void Grid_searcher::create_grid(Grid_t& grid, const Selection &sel){
    if(!is_periodic){
        // Get the minmax of selection
        sel.minmax(min,max);
        // Add a "halo: of size cutoff
        min.array() -= cutoff;
        max.array() += cutoff;
    } else {        
        // Set dimensions of the current unit cell
        min.fill(0.0);
        max = box.extents();
    }

    set_grid_size(min,max, sel.size(), box);
    // Allocate one grid
    grid.resize( boost::extents[NgridX][NgridY][NgridZ] );
    // Allocate visited array
    visited.resize( boost::extents[NgridX][NgridY][NgridZ] );
}


void Grid_searcher::create_grid2(const Selection &sel1, const Selection &sel2){    
    if(!is_periodic){
        // Get the minmax of each selection
        Vector3f min1,min2,max1,max2;

        sel1.minmax(min1,max1);
        sel2.minmax(min2,max2);
        // Add a "halo: of size cutoff for each of them
        min1.array() -= cutoff;
        max1.array() += cutoff;
        min2.array() -= cutoff;
        max2.array() += cutoff;

        // Find true bounding box
        for(int i=0;i<3;++i){
            overlap_1d(min1(i),max1(i),min2(i),max2(i),min(i),max(i));
            // If no overlap just exit
            if(max(i)==min(i)) return;
        }
    } else {
        // Set dimensions of the current unit cell
        min.fill(0.0);
        max = box.extents();
    }

    set_grid_size(min,max, sel1.size()+sel2.size(), box);
    // Allocate both grids
    grid1.resize( boost::extents[NgridX][NgridY][NgridZ] );
    grid2.resize( boost::extents[NgridX][NgridY][NgridZ] );
    // Allocate visited array
    visited.resize( boost::extents[NgridX][NgridY][NgridZ] );
}

// General function for populating given grid from given selection
void Grid_searcher::populate_grid(Grid_t& grid, const Selection &sel){
    int Natoms = sel.size();
    int n1,n2,n3,i,j,k;

    // Clear grid
    for(i=0;i<NgridX;++i)
        for(j=0;j<NgridY;++j)
            for(k=0;k<NgridZ;++k){
                grid[i][j][k].clear();                
            }

    // Assigning atoms to grid
    Vector3f coor;
    if(!is_periodic)
        // Non-periodic variant
        for(i=0;i<Natoms;++i){
            // Get coordinates of atom
            coor = sel.XYZ(i);

            n1 = floor((NgridX-0)*(coor(0)-min(0))/(max(0)-min(0)));
            if(n1<0 || n1>=NgridX) continue;

            n2 = floor((NgridY-0)*(coor(1)-min(1))/(max(1)-min(1)));
            if(n2<0 || n2>=NgridY) continue;

            n3 = floor((NgridZ-0)*(coor(2)-min(2))/(max(2)-min(2)));
            if(n3<0 || n3>=NgridZ) continue;

            grid[n1][n2][n3].push_back(i);
        }
    else {
        // Periodic variant        
        for(i=0;i<Natoms;++i){
            // Get coordinates of atom
            coor = sel.XYZ(i);
            // Get coordinates in triclinic basis if needed
            if(box.is_triclinic()) coor = box.lab_to_box(coor);
            // Assign to non-periodic grid first
            n1 = floor((NgridX-0)*(coor(0)-min(0))/(max(0)-min(0)));
            n2 = floor((NgridY-0)*(coor(1)-min(1))/(max(1)-min(1)));
            n3 = floor((NgridZ-0)*(coor(2)-min(2))/(max(2)-min(2)));

            // Wrap if extends over the grid dimensions
            while(n1>=NgridX || n1<0)
                n1>=0 ? n1 %= NgridX : n1 = NgridX + n1%NgridX;
            while(n2>=NgridY || n2<0)
                n2>=0 ? n2 %= NgridY : n2 = NgridY + n2%NgridY;
            while(n3>=NgridZ || n3<0)
                n3>=0 ? n3 %= NgridZ : n3 = NgridZ + n3%NgridZ;

            // Assign to grid
            grid[n1][n2][n3].push_back(i);
        }
    }
}

void Grid_searcher::populate_coor_grid(Grid_searcher::Grid_coor_t &grid, const Selection &sel)
{
    int Natoms = sel.size();
    int n1,n2,n3,i,j,k;

    // Clear grid
    for(i=0;i<NgridX;++i)
        for(j=0;j<NgridY;++j)
            for(k=0;k<NgridZ;++k){
                grid[i][j][k].clear();
            }

    // Assigning atoms to grid

    if(!is_periodic){
        // Non-periodic variant
        Vector3f* coor;
        for(i=0;i<Natoms;++i){
            // Get coordinates of atom
            coor = sel.XYZ_ptr(i);

            n1 = floor((NgridX-0)*((*coor)(0)-min(0))/(max(0)-min(0)));
            if(n1<0 || n1>=NgridX) continue;

            n2 = floor((NgridY-0)*((*coor)(1)-min(1))/(max(1)-min(1)));
            if(n2<0 || n2>=NgridY) continue;

            n3 = floor((NgridZ-0)*((*coor)(2)-min(2))/(max(2)-min(2)));
            if(n3<0 || n3>=NgridZ) continue;

            grid[n1][n2][n3].push_back(Grid_element(i,coor));
        }
    } else {
        // Periodic variant
        Vector3f coor;
        for(i=0;i<Natoms;++i){
            // Get coordinates of atom
            coor = sel.XYZ(i);
            // Get coordinates in triclinic basis if needed
            if(box.is_triclinic()) coor = box.lab_to_box(coor);
            // Assign to non-periodic grid first
            n1 = floor((NgridX-0)*(coor(0)-min(0))/(max(0)-min(0)));
            n2 = floor((NgridY-0)*(coor(1)-min(1))/(max(1)-min(1)));
            n3 = floor((NgridZ-0)*(coor(2)-min(2))/(max(2)-min(2)));

            // Wrap if extends over the grid dimensions
            while(n1>=NgridX || n1<0)
                n1>=0 ? n1 %= NgridX : n1 = NgridX + n1%NgridX;
            while(n2>=NgridY || n2<0)
                n2>=0 ? n2 %= NgridY : n2 = NgridY + n2%NgridY;
            while(n3>=NgridZ || n3<0)
                n3>=0 ? n3 %= NgridZ : n3 = NgridZ + n3%NgridZ;

            // Assign to grid
            grid[n1][n2][n3].push_back(Grid_element(i,sel.XYZ_ptr(i)));
        }
    }
}

// Search over part of space. To be called in a thread.
void Grid_searcher::do_part1(int dim, int _b, int _e,
                             const Selection &sel,
                             std::vector<Eigen::Vector2i>& bon,
                             std::vector<float>* dist_vec){

    Vector3i b(0,0,0);
    Vector3i e(NgridX,NgridY,NgridZ);
    int dim_max = e(dim);
    b(dim)= _b;
    e(dim)= _e;
    int i,j,k,i1,nlist_size;
    vector<Vector3i> nlist; // Local nlist

    for(i=b(0);i<e(0);++i){
        for(j=b(1);j<e(1);++j){
            for(k=b(2);k<e(2);++k){
                // Search in central cell
                get_central_1(i,j,k, sel, bon, dist_vec);
                visited[i][j][k] = true;
                // Get neighbour list locally
                get_nlist_local(i,j,k,nlist);
                nlist_size = nlist.size();
                // Search between this and neighbouring cells
                for(i1=0;i1<nlist_size;++i1){
                    // If the neighbour is "at left" from the boundary of this part,
                    // ignore it. Only consider dim dimension.

                    //if(nlist[i1](dim)<b(dim) || (b(dim)==0 && nlist[i1](dim)==dim_max-1)){
                    if(nlist[i1](dim)<b(dim)){
                        continue;
                    }

                    // We only check for visited cells inside local part, not in the "halo"
                    if(    nlist[i1](dim)>=b(dim)
                        && nlist[i1](dim)<e(dim) ){
                        // cell is inside the partition
                        if( !visited[nlist[i1](0)][nlist[i1](1)][nlist[i1](2)] )
                            get_side_1(i,j,k, nlist[i1](0),nlist[i1](1),nlist[i1](2),sel, bon, dist_vec);
                    } else {
                        // cell is in halo
                        get_side_1(i,j,k, nlist[i1](0),nlist[i1](1),nlist[i1](2),sel, bon, dist_vec);
                    }


                }

            }
        }
    }
}

// Search inside one selection
void Grid_searcher::do_search(const Selection &sel, std::vector<Eigen::Vector2i>& bon,
                              std::vector<float>* dist_vec){
    int i,j,k,i1;
    int nlist_size;

    // Search
    bon.clear();
    if(dist_vec) dist_vec->clear();

    // Init visited cells array
    for(i=0;i<NgridX;++i)
        for(j=0;j<NgridY;++j)
            for(k=0;k<NgridZ;++k)
                visited[i][j][k] = false;

    // See if we need parallelization
    int max_N, max_dim;
    Vector3i dims(NgridX,NgridY,NgridZ);
    max_N = dims.maxCoeff(&max_dim);

    int nt = std::min(max_N, int(std::thread::hardware_concurrency()));

    if(nt==1){
    //if(nt>0){
        // Serial searching
        for(i=0;i<NgridX;++i){
            for(j=0;j<NgridY;++j){
                for(k=0;k<NgridZ;++k){
                    // Search in central cell
                    get_central_1(i,j,k, sel, bon, dist_vec);
                    visited[i][j][k] = true;
                    // Get neighbour list
                    get_nlist(i,j,k);
                    nlist_size = nlist.size();
                    // Searh between this and neighbouring cells
                    for(i1=0;i1<nlist_size;++i1){
                        //cout << nlist[i1].transpose() << endl;
                        if( !visited[nlist[i1](0)][nlist[i1](1)][nlist[i1](2)] )
                            get_side_1(i,j,k, nlist[i1](0),nlist[i1](1),nlist[i1](2),sel, bon, dist_vec);
                    }
                }
            }
        }

    } else {
        // Parallel searching

        // Determine parts for each thread
        vector<int> b(nt),e(nt);
        int cur=0;
        for(int i=0;i<nt-1;++i){
            b[i]=cur;
            cur += dims(max_dim)/nt;
            e[i]=cur;
        }
        b[nt-1]=cur;
        e[nt-1]=dims(max_dim);

        //for(int i=0;i<nt;++i) cout << b[i] << ":" << e[i] << " ";
        //cout << endl;

        // Prepare arrays per each thread
        vector< vector<Vector2i> > _bon(nt);
        vector< vector<float> > _dist_vec(nt);
        vector< vector<float>* > _dist_vec_ptr(nt);
        for(int i=0;i<nt;++i) _dist_vec_ptr[i] = dist_vec ? &_dist_vec[i] : nullptr;

        // Launch threads
        vector<thread> threads;
        for(int i=0;i<nt;++i){
            threads.push_back( thread(
                                   std::bind(
                                       &Grid_searcher::do_part1,
                                       this,
                                       max_dim,
                                       b[i],
                                       e[i],
                                       sel,
                                       ref(_bon[i]),
                                       ref(_dist_vec_ptr[i])
                                   )
                                )
                             );
        }

        // Wait for threads
        for(auto& t: threads) t.join();

        // Collect results
        for(int i=0;i<nt;++i){
            copy(_bon[i].begin(),_bon[i].end(), back_inserter(bon));
        }
        if(dist_vec){
            for(int i=0;i<nt;++i)
                copy(_dist_vec[i].begin(),_dist_vec[i].end(), back_inserter(*dist_vec));
        }
    }
}

// Search over part of space for two selections. To be called in a thread.
void Grid_searcher::do_part2(int dim, int _b, int _e,
                             const Selection &sel1, const Selection &sel2,
                             std::vector<Eigen::Vector2i>& bon,
                             std::vector<float>* dist_vec){

    Vector3i b(0,0,0);
    Vector3i e(NgridX,NgridY,NgridZ);
    int dim_max = e(dim);
    b(dim)= _b;
    e(dim)= _e;
    int i,j,k,i1,nlist_size;
    vector<Vector3i> nlist; // Local nlist

    for(i=b(0);i<e(0);++i){
        for(j=b(1);j<e(1);++j){
            for(k=b(2);k<e(2);++k){
                // Search in central cell
                get_central_2(i,j,k, sel1, sel2, bon, dist_vec);
                visited[i][j][k] = true;
                // Get neighbour list locally
                get_nlist_local(i,j,k,nlist);
                nlist_size = nlist.size();
                // Search between this and neighbouring cells
                for(i1=0;i1<nlist_size;++i1){
                    // If the neighbour is "at left" from the boundary of this part,
                    // ignore it. Only consider dim dimension.

                    //if(nlist[i1](dim)<b(dim) || (b(dim)==0 && nlist[i1](dim)==dim_max-1)){
                    if(nlist[i1](dim)<b(dim)){
                        continue;
                    }

                    // We only check for visited cells inside local part, not in the "halo"
                    if(    nlist[i1](dim)>=b(dim)
                        && nlist[i1](dim)<e(dim) ){
                        // cell is inside the partition
                        if( !visited[nlist[i1](0)][nlist[i1](1)][nlist[i1](2)] )
                            get_side_2(i,j,k, nlist[i1](0),nlist[i1](1),nlist[i1](2),sel1, sel2, bon, dist_vec);
                    } else {
                        // cell is in halo
                        get_side_2(i,j,k, nlist[i1](0),nlist[i1](1),nlist[i1](2),sel1, sel2, bon, dist_vec);
                    }


                }

            }
        }
    }
}

// Search between two selections
void Grid_searcher::do_search(const Selection &sel1, const Selection &sel2, std::vector<Eigen::Vector2i>& bon,
                              std::vector<float>* dist_vec){
    int i,j,k,nlist_size,i1;
    int n1,n2,n3;

    // Search
    bon.clear();    
    if(dist_vec) dist_vec->clear();

    // Init visited cells array
    for(i=0;i<NgridX;++i)
        for(j=0;j<NgridY;++j)
            for(k=0;k<NgridZ;++k)
                visited[i][j][k] = false;

    // See if we need parallelization
    int max_N, max_dim;
    Vector3i dims(NgridX,NgridY,NgridZ);
    max_N = dims.maxCoeff(&max_dim);

    int nt = std::min(max_N, int(std::thread::hardware_concurrency()));

    if(nt==1){
    //if(nt>0){
        // Serial search
        for(i=0;i<NgridX;++i){
            for(j=0;j<NgridY;++j){
                for(k=0;k<NgridZ;++k){
                    // Search in central cell
                    get_central_2(i,j,k, sel1, sel2, bon, dist_vec);
                    visited[i][j][k] = true;
                    // Get neighbour list
                    get_nlist(i,j,k);
                    nlist_size = nlist.size();
                    // Searh between this and neighbouring cells
                    for(i1=0;i1<nlist_size;++i1){
                        //cout << nlist[i1].transpose()<<endl;
                        if( !visited[nlist[i1](0)][nlist[i1](1)][nlist[i1](2)] )
                            get_side_2(i,j,k, nlist[i1](0),nlist[i1](1),nlist[i1](2),
                                       sel1, sel2, bon, dist_vec);
                    }

                }
            }
        }
    } else {
        // Search in parallel
        // Determine parts for each thread
        vector<int> b(nt),e(nt);
        int cur=0;
        for(int i=0;i<nt-1;++i){
            b[i]=cur;
            cur += dims(max_dim)/nt;
            e[i]=cur;
        }
        b[nt-1]=cur;
        e[nt-1]=dims(max_dim);

        //for(int i=0;i<nt;++i) cout << b[i] << ":" << e[i] << " ";
        //cout << endl;

        // Prepare arrays per each thread
        vector< vector<Vector2i> > _bon(nt);
        vector< vector<float> > _dist_vec(nt);
        vector< vector<float>* > _dist_vec_ptr(nt);
        for(int i=0;i<nt;++i) _dist_vec_ptr[i] = dist_vec ? &_dist_vec[i] : nullptr;

        // Launch threads
        vector<thread> threads;
        for(int i=0;i<nt;++i){
            threads.push_back( thread(
                                   std::bind(
                                       &Grid_searcher::do_part2,
                                       this,
                                       max_dim,
                                       b[i],
                                       e[i],
                                       sel1,
                                       sel2,
                                       ref(_bon[i]),
                                       ref(_dist_vec_ptr[i])
                                   )
                                )
                             );
        }

        // Wait for threads
        for(auto& t: threads) t.join();

        // Collect results
        for(int i=0;i<nt;++i){
            copy(_bon[i].begin(),_bon[i].end(), back_inserter(bon));
        }
        if(dist_vec){
            for(int i=0;i<nt;++i)
                copy(_dist_vec[i].begin(),_dist_vec[i].end(), back_inserter(*dist_vec));
        }
    }
}


// Search in central cell inside 1 selection
void Grid_searcher::get_central_1(int i1, int j1, int k1, const Selection &sel,
                                    std::vector<Eigen::Vector2i>& bonds,
                                    std::vector<float>* dist_vec){
    int c1,c2;
    int n1 = grid1[i1][j1][k1].size();

    if(n1==0) return; //Nothing to do

    float d;
    Vector2i pair;
    Vector3f p1,p2;

    for(c1=0;c1<n1-1;++c1)
        for(c2=c1+1;c2<n1;++c2){
            if(!is_periodic)
                // Get non-periodic distance
                d = (sel.XYZ(grid1[i1][j1][k1][c1]) -
                     sel.XYZ(grid1[i1][j1][k1][c2])).norm();
            else
                // Get periodic distance
                d = box.distance(sel.XYZ(grid1[i1][j1][k1][c1]),
                                      sel.XYZ(grid1[i1][j1][k1][c2]));

            if(d<=cutoff){
                if(abs_index){
                    pair(0) = sel.Index(grid1[i1][j1][k1][c1]);
                    pair(1) = sel.Index(grid1[i1][j1][k1][c2]);
                } else {
                    pair(0) = grid1[i1][j1][k1][c1];
                    pair(1) = grid1[i1][j1][k1][c2];
                }

                bonds.push_back(pair); //Add bond
                if(dist_vec) dist_vec->push_back(d);
            }
        }
}

// Add bonds between two given cells of the grid for single selection
void Grid_searcher::get_side_1(int i1, int j1, int k1, int i2, int j2, int k2, const Selection &sel,
                                std::vector<Eigen::Vector2i>& bonds,
                                std::vector<float>* dist_vec){

    int c1,c2;
    int n1 = grid1[i1][j1][k1].size();
    int n2 = grid1[i2][j2][k2].size();

    if(n1==0 || n2==0) return; //Nothing to do

    float d;
    Vector2i pair;

    for(c1=0;c1<n1;++c1)
        for(c2=0;c2<n2;++c2){
            if(!is_periodic)
                d = (sel.XYZ(grid1[i1][j1][k1][c1]) -
                     sel.XYZ(grid1[i2][j2][k2][c2])).norm();
            else
                d = box.distance(sel.XYZ(grid1[i1][j1][k1][c1]),
                                      sel.XYZ(grid1[i2][j2][k2][c2]));

            if(d<=cutoff){
                if(abs_index){
                    pair(0) = sel.Index(grid1[i1][j1][k1][c1]);
                    pair(1) = sel.Index(grid1[i2][j2][k2][c2]);
                } else {
                    pair(0) = grid1[i1][j1][k1][c1];
                    pair(1) = grid1[i2][j2][k2][c2];
                }
                bonds.push_back(pair);
                if(dist_vec) dist_vec->push_back(d);
            }
        }
}

void Grid_searcher::get_nlist(int i,int j,int k){

    nlist.clear();

    Vector3i coor;

    if(!is_periodic){
        int c1,c2,c3;
        // Non-periodic variant
        for(c1=-1; c1<=1; ++c1){
            coor(0) = i+c1;
            if(coor(0)<0 || coor(0)>=NgridX) continue; // Bounds check
            for(c2=-1; c2<=1; ++c2){
                coor(1) = j+c2;
                if(coor(1)<0 || coor(1)>=NgridY) continue; // Bounds check
                for(c3=-1; c3<=1; ++c3){
                    coor(2) = k+c3;
                    if(coor(2)<0 || coor(2)>=NgridZ) continue; // Bounds check
                    //Exclude central cell
                    if(coor(0) == i && coor(1) == j && coor(2) == k ) continue;
                    // Add cell
                    nlist.push_back(coor);
                }
            }
        }
    } else {
        // Periodic variant
        int bX = 0, eX = 0;
        int bY = 0, eY = 0;
        int bZ = 0, eZ = 0;

        // If the number of cells in dimension is 2 this is a special case
        // when only one neighbour is need. Otherwise add both.        
        if(NgridX>1) bX = -1;
        if(NgridY>1) bY = -1;
        if(NgridZ>1) bZ = -1;

        if(NgridX>2) eX = 1;
        if(NgridY>2) eY = 1;
        if(NgridZ>2) eZ = 1;

        int c1,c2,c3;

        for(c1 = bX; c1<=eX; ++c1){
            coor(0) = i+c1;
            if(coor(0)==NgridX) coor(0) = 0;
            if(coor(0)==-1) coor(0) = NgridX-1;
            for(c2 = bY; c2<=eY; ++c2){
                coor(1) = j+c2;
                if(coor(1)==NgridY) coor(1) = 0;
                if(coor(1)==-1) coor(1) = NgridY-1;
                for(c3 = bZ; c3<=eZ; ++c3){
                    coor(2) = k+c3;
                    if(coor(2)==NgridZ) coor(2) = 0;
                    if(coor(2)==-1) coor(2) = NgridZ-1;
                    //Exclude central cell
                    if(coor(0) == i && coor(1) == j && coor(2) == k) continue;
                    // Add cell
                    nlist.push_back(coor);
                }
            }
        }
    }
}

std::array<Eigen::Vector3i,13> directions = {
    Vector3i{1,1,1},    //1
    Vector3i{1,1,0},    //2
    Vector3i{1,1,-1},   //3
    Vector3i{1,0,1},    //4
    Vector3i{1,0,0},    //5
    Vector3i{1,-1,1},   //6
    Vector3i{0,0,1},    //7
    Vector3i{0,1,1},    //8
    Vector3i{0,1,0},    //9
    Vector3i{0,-1,1},   //10    
    Vector3i{-1,1,0},  //11
    Vector3i{-1,0,1},   //12
    Vector3i{-1,1,1}    //13
};

// Get 13 directions, which are 1/2 of the whole 27-cube without central cell
void Grid_searcher::get_nlist_13(int x,int y,int z, std::vector<Eigen::Vector3i>& nlist){
    nlist.clear();

    if(!is_periodic){
        int px,py,pz;

        for(int i=0;i<directions.size();++i){
            px = x+directions[i](0);
            py = y+directions[i](1);
            pz = z+directions[i](2);
            if(px>=0 && px<NgridX && py>=0 && py<NgridY && pz>=0 && pz<NgridZ)
                nlist.push_back( Vector3i(px,py,pz) );
        }

    } else {

    }
}


void Grid_searcher::get_nlist_local(int i,int j,int k, std::vector<Eigen::Vector3i>& nlist){

    nlist.clear();

    Vector3i coor;

    if(!is_periodic){
        int c1,c2,c3;
        // Non-periodic variant
        for(c1=-1; c1<=1; ++c1){
            coor(0) = i+c1;
            if(coor(0)<0 || coor(0)>=NgridX) continue; // Bounds check
            for(c2=-1; c2<=1; ++c2){
                coor(1) = j+c2;
                if(coor(1)<0 || coor(1)>=NgridY) continue; // Bounds check
                for(c3=-1; c3<=1; ++c3){
                    coor(2) = k+c3;
                    if(coor(2)<0 || coor(2)>=NgridZ) continue; // Bounds check
                    //Exclude central cell
                    if(coor(0) == i && coor(1) == j && coor(2) == k ) continue;
                    // Add cell
                    nlist.push_back(coor);
                }
            }
        }
    } else {
        // Periodic variant
        int bX = 0, eX = 0;
        int bY = 0, eY = 0;
        int bZ = 0, eZ = 0;

        // If the number of cells in dimension is 2 this is a special case
        // when only one neighbour is need. Otherwise add both.
        if(NgridX>1) bX = -1;
        if(NgridY>1) bY = -1;
        if(NgridZ>1) bZ = -1;

        if(NgridX>2) eX = 1;
        if(NgridY>2) eY = 1;
        if(NgridZ>2) eZ = 1;

        int c1,c2,c3;

        for(c1 = bX; c1<=eX; ++c1){
            coor(0) = i+c1;
            if(coor(0)==NgridX) coor(0) = 0;
            if(coor(0)==-1) coor(0) = NgridX-1;
            for(c2 = bY; c2<=eY; ++c2){
                coor(1) = j+c2;
                if(coor(1)==NgridY) coor(1) = 0;
                if(coor(1)==-1) coor(1) = NgridY-1;
                for(c3 = bZ; c3<=eZ; ++c3){
                    coor(2) = k+c3;
                    if(coor(2)==NgridZ) coor(2) = 0;
                    if(coor(2)==-1) coor(2) = NgridZ-1;
                    //Exclude central cell
                    if(coor(0) == i && coor(1) == j && coor(2) == k) continue;
                    // Add cell
                    nlist.push_back(coor);
                }
            }
        }
    }
}

// Add bonds inside one cell for two selections
void Grid_searcher::get_central_2(int i1, int j1, int k1, const Selection &sel1, const Selection &sel2,
                                    std::vector<Eigen::Vector2i>& bonds,
                                    std::vector<float>* dist_vec){
    int c1,c2;
    int n1 = grid1[i1][j1][k1].size();
    int n2 = grid2[i1][j1][k1].size();
    if(n1==0 || n2==0) return; //Nothing to do

    float d;
    VectorXi pair(2);

    for(c1=0;c1<n1;++c1)
        for(c2=0;c2<n2;++c2){
            if(!is_periodic)
                d = (sel1.XYZ(grid1[i1][j1][k1][c1]) - sel2.XYZ(grid2[i1][j1][k1][c2])).norm();
            else
                d = box.distance(sel1.XYZ(grid1[i1][j1][k1][c1]),
                                 sel2.XYZ(grid2[i1][j1][k1][c2]));

            if(d<=cutoff){
                if(abs_index){
                    pair(0) = sel1.Index(grid1[i1][j1][k1][c1]);
                    pair(1) = sel2.Index(grid2[i1][j1][k1][c2]);
                } else {
                    pair(0) = grid1[i1][j1][k1][c1];
                    pair(1) = grid2[i1][j1][k1][c2];
                }
                bonds.push_back(pair);
                if(dist_vec) dist_vec->push_back(d);
            }
        }
}

// Add contacts between two given cells of the grid for two selections
void Grid_searcher::get_side_2(int i1, int j1, int k1, int i2, int j2, int k2,
                                const Selection &sel1, const Selection &sel2,
                                std::vector<Eigen::Vector2i>& bonds,
                                std::vector<float>* dist_vec){
    int c1,c2;
    float d;
    Vector2i pair;

    // First phase. Search for contacts between sel1 in cell1 and sel2 in cell2
    int n1 = grid1[i1][j1][k1].size();
    int n2 = grid2[i2][j2][k2].size();

    if(n1*n2!=0){ // If both cells are not empty
        for(c1=0;c1<n1;++c1)
            for(c2=0;c2<n2;++c2){

                if(!is_periodic)
                    d = (sel1.XYZ(grid1[i1][j1][k1][c1]) - sel2.XYZ(grid2[i2][j2][k2][c2])).norm();
                else {
                    d = box.distance(sel1.XYZ(grid1[i1][j1][k1][c1]),
                                     sel2.XYZ(grid2[i2][j2][k2][c2]));
                }

                if(d<=cutoff){
                    if(abs_index){
                        pair(0) = sel1.Index(grid1[i1][j1][k1][c1]);
                        pair(1) = sel2.Index(grid2[i2][j2][k2][c2]);
                    } else {
                        pair(0) = grid1[i1][j1][k1][c1];
                        pair(1) = grid2[i2][j2][k2][c2];
                    }
                    bonds.push_back(pair);
                    if(dist_vec) dist_vec->push_back(d);
                }
            }
    }

    // Second phase. Search for contacts between sel2 in cell1 and sel1 in cell2
    n1 = grid2[i1][j1][k1].size();
    n2 = grid1[i2][j2][k2].size();

    if(n1*n2!=0){ // If cells are not empty
        for(c1=0;c1<n1;++c1)
            for(c2=0;c2<n2;++c2){
                if(!is_periodic)
                    d = (sel2.XYZ(grid2[i1][j1][k1][c1]) - sel1.XYZ(grid1[i2][j2][k2][c2])).norm();
                else {
                    d = box.distance(sel2.XYZ(grid2[i1][j1][k1][c1]),
                                     sel1.XYZ(grid1[i2][j2][k2][c2]));
                }

                if(d<=cutoff){
                    if(abs_index){
                        pair(1) = sel2.Index(grid2[i1][j1][k1][c1]); //ordered pair!
                        pair(0) = sel1.Index(grid1[i2][j2][k2][c2]);
                    } else {
                        pair(1) = grid2[i1][j1][k1][c1]; //ordered pair!
                        pair(0) = grid1[i2][j2][k2][c2];
                    }
                    bonds.push_back(pair);
                    if(dist_vec) dist_vec->push_back(d);
                }
            }
    }
}
