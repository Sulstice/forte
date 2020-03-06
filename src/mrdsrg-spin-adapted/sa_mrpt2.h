/*
 * @BEGIN LICENSE
 *
 * Forte: an open-source plugin to Psi4 (https://github.com/psi4/psi4)
 * that implements a variety of quantum chemistry methods for strongly
 * correlated electrons.
 *
 * Copyright (c) 2012-2020 by its authors (see COPYING, COPYING.LESSER, AUTHORS).
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * @END LICENSE
 */

#ifndef _sa_mrdsrg_h_
#define _sa_mrdsrg_h_

#include <cmath>
#include <memory>

#include "psi4/libdiis/diismanager.h"

#include "psi4/libmints/wavefunction.h"
#include "psi4/libpsio/psio.hpp"
#include "psi4/libpsio/psio.h"
#include "ambit/blocked_tensor.h"

#include "integrals/integrals.h"
#include "base_classes/rdms.h"
#include "helpers/blockedtensorfactory.h"
#include "sparse_ci/determinant.h"
#include "sadsrg.h"

using namespace ambit;

namespace forte {

class SA_MRPT2 : public SADSRG {
  public:
    /**
     * SA_MRPT2 Constructor
     * @param scf_info The SCFInfo
     * @param options The ForteOption
     * @param ints A pointer to an allocated integral object
     * @param mo_space_info The MOSpaceInfo object
     */
    SA_MRPT2(RDMs rdms, std::shared_ptr<SCFInfo> scf_info, std::shared_ptr<ForteOptions> options,
             std::shared_ptr<ForteIntegrals> ints, std::shared_ptr<MOSpaceInfo> mo_space_info);

    /// Compute the corr_level energy with fixed reference
    virtual double compute_energy();

  protected:
    // => Class initialization and termination <= //

    /// Start-up function called in the constructor
    void startup();

    /// Read options
    void read_options();

    /// Print a summary of the options
    void print_options();

    /// Fill up integrals
    void build_ints();

    /// Are orbitals semi-canonicalized?
    bool semi_canonical_;

    /// Include internal amplitudes or not?
    std::string internal_amp_;
    /// Include which part of internal amplitudes?
    std::string internal_amp_select_;

    // => DSRG related <= //

    /// One-electron integral
    ambit::BlockedTensor H_;
    /// Two-electron integral
    ambit::BlockedTensor V_;
    /// Three-index integrals
    ambit::BlockedTensor B_;
    /// Generalized Fock matrix
    ambit::BlockedTensor F_;
    /// Single excitation amplitude
    ambit::BlockedTensor T1_;
    /// Double excitation amplitude
    ambit::BlockedTensor T2_;

    /// Unitary matrix to block diagonal Fock
    ambit::BlockedTensor U_;

    /// compute 1st-order T2 amplitudes
    void compute_t2();
    /// compute 1st-order T2 amplitudes with at least two active indices
    void compute_t2_minimal();

    /// compute 1st-order T1 amplitudes
    void compute_t1();

    /// Compute DSRG-transformed Hamiltonian
    void compute_hbar();
    /// Compute Hbar1 from core contraction when doing DiskDF
    void compute_Hbar1C_diskDF(ambit::BlockedTensor& Hbar1);
    /// Compute Hbar1 from virtual contraction when doing DiskDF
    void compute_Hbar1V_diskDF(ambit::BlockedTensor& Hbar1);
};
} // namespace forte

#endif // _sa_mrdsrg_h_
