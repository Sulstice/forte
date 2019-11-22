#include "casscf/casscf.h"
#include "casscf/backtransform_tpdm.h"
#include "ambit/tensor.h"
#include "ambit/blocked_tensor.h"

#include "integrals/integrals.h"
#include "base_classes/rdms.h"

#include "psi4/libfock/jk.h"
#include "psi4/libmints/matrix.h"
#include "psi4/libmints/wavefunction.h"
#include "psi4/libqt/qt.h"
#include "psi4/psifiles.h"

#include "helpers/printing.h"
#include "helpers/helpers.h"
#include "helpers/timer.h"
#include "sci/aci.h"
#include "fci/fci_solver.h"
#include "base_classes/active_space_solver.h"

#include "sci/fci_mo.h"
#include "orbital-helpers/mp2_nos.h"
#include "orbital-helpers/orbitaloptimizer.h"
#include "orbital-helpers/semi_canonicalize.h"

#ifdef HAVE_CHEMPS2
#include "dmrg/dmrgsolver.h"
#endif
#include "psi4/libdiis/diisentry.h"
#include "psi4/libdiis/diismanager.h"
#include "psi4/libmints/factory.h"


#include "psi4/psifiles.h"
#include "psi4/libiwl/iwl.hpp"
#include "psi4/libpsio/psio.hpp"

using namespace ambit;
using namespace psi;

namespace forte 
{

/**
/* Set up MO spaces using ambit. 
*/
void CASSCF::set_ambit_space() {


    outfile->Printf("\n    Setting ambit MO space .......................... ");
    BlockedTensor::reset_mo_spaces();
    BlockedTensor::set_expert_mode(true);

    core_mos_ = mo_space_info_->get_corr_abs_mo("RESTRICTED_DOCC");
    actv_mos_ = mo_space_info_->get_corr_abs_mo("ACTIVE");
    virt_mos_ = mo_space_info_->get_corr_abs_mo("RESTRICTED_UOCC");

    // define space labels
    acore_label_ = "c";
    aactv_label_ = "a";
    avirt_label_ = "v";
    bcore_label_ = "C";
    bactv_label_ = "A";
    bvirt_label_ = "V";

    // add Ambit index labels
    BTF_= std::make_shared<BlockedTensorFactory>();
    BTF_->add_mo_space(acore_label_, "mn$%", core_mos_, AlphaSpin);
    BTF_->add_mo_space(bcore_label_, "MN<>", core_mos_, BetaSpin);
    BTF_->add_mo_space(aactv_label_, "uvwxyz123", actv_mos_, AlphaSpin);
    BTF_->add_mo_space(bactv_label_, "UVWXYZ!@#", actv_mos_, BetaSpin);
    BTF_->add_mo_space(avirt_label_, "ef", virt_mos_, AlphaSpin);
    BTF_->add_mo_space(bvirt_label_, "EF", virt_mos_, BetaSpin);

    // define composite spaces
    BTF_->add_composite_mo_space("h", "ijkl", {acore_label_, aactv_label_});
    BTF_->add_composite_mo_space("H", "IJKL", {bcore_label_, bactv_label_});
    BTF_->add_composite_mo_space("p", "abcd", {aactv_label_, avirt_label_});
    BTF_->add_composite_mo_space("P", "ABCD", {bactv_label_, bvirt_label_});
    BTF_->add_composite_mo_space("g", "pqrsto456", {acore_label_, aactv_label_, avirt_label_});
    BTF_->add_composite_mo_space("G", "PQRSTO789", {bcore_label_, bactv_label_, bvirt_label_});

    outfile->Printf("Done");	
}

void CASSCF::init_variable()
{

}



/**
/* Set one-body and two-body densities.
*/
void CASSCF::set_density() {

    outfile->Printf("\n    Initializing density tensors .................... ");
    Gamma1_ = BTF_->build(CoreTensor, "Gamma1", spin_cases({"aa"}));
    Gamma2_ = BTF_->build(CoreTensor, "Gamma2", spin_cases({"aaaa"}));
    fill_density();
    outfile->Printf("Done");    
}

/**
/* Fill one-body and two-body densities.
*/
void CASSCF::fill_density() {
    // 1-body density 
    Gamma1_.block("aa")("pq") = cas_ref_.g1a()("pq");
    Gamma1_.block("AA")("pq") = cas_ref_.g1b()("pq");


    // 2-body density 
    Gamma2_.block("aaaa")("pqrs") = cas_ref_.g2aa()("pqrs");
    Gamma2_.block("aAaA")("pqrs") = cas_ref_.g2ab()("pqrs");
    Gamma2_.block("AAAA")("pqrs") = cas_ref_.g2bb()("pqrs");

}


/**
/* Set one-electron integrals.
*/
void CASSCF::set_h() {

    outfile->Printf("\n    Building Hamiltonian ............................ ");
    H_ = BTF_->build(ambit::CoreTensor, "Hamiltonian", spin_cases({"gg"}));

    H_.iterate([&](const std::vector<size_t>& i, const std::vector<SpinType>& spin, double& value) {
        if (spin[0] == AlphaSpin) {
            value = ints_->oei_a(i[0], i[1]);
        } 
        else {
            value = ints_->oei_b(i[0], i[1]);
        }
    });

    outfile->Printf("Done");
}


/**
/* Set two electron repulsion integrals.
*/
void CASSCF::set_v() {

    outfile->Printf("\n    Building electron repulsion integrals ........... ");
    V_ = BTF_->build(ambit::CoreTensor, "Hamiltonian", spin_cases({"gggg"}));

    V_.iterate([&](const std::vector<size_t>& i, const std::vector<SpinType>& spin, double& value) {
        if (spin[0] == AlphaSpin && spin[1] == AlphaSpin) {
            value = ints_->aptei_aa(i[0], i[1], i[2], i[3]);
        } 
        else if (spin[0] == AlphaSpin && spin[1] == BetaSpin) {
            value = ints_->aptei_ab(i[0], i[1], i[2], i[3]);
        }
        else if (spin[0] == BetaSpin && spin[1] == BetaSpin) {
            value = ints_->aptei_bb(i[0], i[1], i[2], i[3]);
        }
    });

    outfile->Printf("Done");
}


/**
/* Set Fock matrices.
*/
void CASSCF::set_fock() {

    outfile->Printf("\n    Building Fock matrix ............................ ");

    F_ = BTF_->build(ambit::CoreTensor, "Fock", spin_cases({"gg"}));

    psi::SharedMatrix D1a(new psi::Matrix("D1a", nmo_, nmo_));
    psi::SharedMatrix D1b(new psi::Matrix("D1b", nmo_, nmo_));
    for (size_t m = 0, ncore = core_mos_.size(); m < ncore; m++) {
        D1a->set(core_mos_[m], core_mos_[m], 1.0);
        D1b->set(core_mos_[m], core_mos_[m], 1.0);
    }

    Gamma1_.block("aa").citerate([&](const std::vector<size_t>& i, const double& value) {
        D1a->set(actv_mos_[i[0]], actv_mos_[i[1]], value);
    });
    Gamma1_.block("AA").citerate([&](const std::vector<size_t>& i, const double& value) {
        D1b->set(actv_mos_[i[0]], actv_mos_[i[1]], value);
    });


    ints_->make_fock_matrix(D1a, D1b);

    F_.iterate([&](const std::vector<size_t>& i, const std::vector<SpinType>& spin, double& value) {
        if (spin[0] == AlphaSpin) {
            value = ints_->get_fock_a(i[0], i[1]);
        } 
        else {
            value = ints_->get_fock_b(i[0], i[1]);
        }
    });    

    outfile->Printf("Done");
}

/**
/* Initialize and set the Lagrangian.
*/
void CASSCF::set_lagrangian() {

    outfile->Printf("\n    Building Lagrangian ............................. ");
    W_ = BTF_->build(CoreTensor, "Lagrangian", spin_cases({"gg"}));
    set_lagrangian_cx();
    set_lagrangian_aa();
    outfile->Printf("Done");
}


/**
/* Set core-core and core-active block entries of Lagrangian.
*/
void CASSCF::set_lagrangian_cx() {

    W_["mp"] = F_["mp"];
    W_["MP"] = F_["MP"];

}


/**
/* Set active-active block entries of Lagrangian.
*/
void CASSCF::set_lagrangian_aa() {

    BlockedTensor temp = BTF_->build(CoreTensor, "temporal tensor", spin_cases({"gg"}));
    BlockedTensor I = BTF_->build(CoreTensor, "identity matrix", spin_cases({"gg"}));

    I.iterate([&](const std::vector<size_t>& i, const std::vector<SpinType>& , double& value) {
        value = (i[0] == i[1]) ? 1.0 : 0.0;
    });

    temp["vp"] = H_["vp"];
    temp["vp"] += V_["vmpn"] * I["mn"];
    temp["vp"] += V_["vMpN"] * I["MN"];
    W_["up"] += temp["vp"] * Gamma1_["uv"];
    W_["up"] += 0.5 * V_["xypv"] * Gamma2_["uvxy"];
    W_["up"] += V_["xYpV"] * Gamma2_["uVxY"];

    temp["VP"] = H_["VP"];
    temp["VP"] += V_["mVnP"] * I["mn"];
    temp["VP"] += V_["VMPN"] * I["MN"];
    W_["UP"] += temp["VP"] * Gamma1_["UV"];
    W_["UP"] += 0.5 * V_["XYPV"] * Gamma2_["UVXY"];
    W_["UP"] += V_["yXvP"] * Gamma2_["vUyX"];

    //need to add symmetric parts !!!!!!!

}






/**
/* Set densities, one-electron integrals, two-electron integrals and Fock matrices.
*/
void CASSCF::set_tensor() {
    set_density();
    set_h();
    set_v();
    set_fock();
}



/**
/* The procedure of computing gradients
*/
SharedMatrix CASSCF::compute_gradient() {

	set_ambit_space();
    set_tensor();
    write_lagrangian();
    write_1rdm_spin_dependent();
    write_2rdm_spin_dependent();
    


    // This line of code is to deceive Psi4 and avoid computing scf gradient
    ints_->wfn()->set_reference_wavefunction(ints_->wfn());







    std::vector<std::shared_ptr<psi::MOSpace> > spaces;
    spaces.push_back(psi::MOSpace::all);
    std::shared_ptr<TPDMBackTransform> transform = std::shared_ptr<TPDMBackTransform>(
    new TPDMBackTransform(ints_->wfn(), spaces,
                IntegralTransform::TransformationType::Unrestricted, // Transformation type
                IntegralTransform::OutputType::DPDOnly,              // Output buffer
                IntegralTransform::MOOrdering::QTOrder,              // MO ordering
                IntegralTransform::FrozenOrbitals::None));           // Frozen orbitals?
    
    // transform->set_psio(ints_->wfn()->psio());
    transform->backtransform_density();
    transform.reset();
    outfile->Printf("\n    TPD Backtransformation .......................... Done");
    outfile->Printf("\n    Computing gradients ............................. Done\n");

    return std::make_shared<Matrix>("nullptr", 0, 0);
}



/**
/* Write spin_dependent one-RDMs coefficients.
*/
void CASSCF::write_1rdm_spin_dependent() {

    outfile->Printf("\n    Computing 1rdm coefficients ..................... ");

    SharedMatrix D1(new Matrix("1rdm coefficients contribution", nmo_, nmo_));

    // We force "Da == Db". The code below need changed if this constraint is revoked.

    std::vector<size_t> core_all_ = mo_space_info_->get_absolute_mo("RESTRICTED_DOCC");
    std::vector<size_t> actv_all_ = mo_space_info_->get_absolute_mo("ACTIVE");
    std::vector<size_t> virt_all_ = mo_space_info_->get_absolute_mo("RESTRICTED_UOCC");

    for(size_t i = 0, size_c = core_all_.size(); i < size_c; ++i) {
        auto m = core_all_[i];
        D1->set(m, m, 1.0);
    }

    for(size_t i = 0, size_a = actv_all_.size(); i < size_a; ++i) {
        for(size_t j = 0; j < size_a; ++j) {
            auto u = actv_all_[i];
            auto v = actv_all_[j];
            D1->set(u, v, Gamma1_.block("aa").data()[i * na_ + j]);
        }
    }



    D1->back_transform(ints_->Ca());

    ints_->wfn()->Da()->copy(D1);
    ints_->wfn()->Db()->copy(ints_->wfn()->Da());

    outfile->Printf("Done");
}



/**
/* Write the Lagrangian.
/*
/* This function needs to be changed if frozen approximation is considered!
*/
void CASSCF::write_lagrangian() {

    set_lagrangian();

    outfile->Printf("\n    Computing Lagrangian ............................ ");
    //backtransform

    SharedMatrix L(new Matrix("Lagrangian", nmo_, nmo_));


    W_.iterate([&](const std::vector<size_t>& i, const std::vector<SpinType>& spin, double& value) {           
        L->add(i[0], i[1], value);
    }); 

    L->back_transform(ints_->Ca());
    ints_->wfn()->Lagrangian()->copy(L);
    outfile->Printf("Done");
}





/**
/* Write spin_dependent two-RDMs coefficients using IWL.
/*
/* Coefficients in d2aa and d2bb need be multiplied with additional 1/2! 
*/
void CASSCF::write_2rdm_spin_dependent() {

    outfile->Printf("\n    Writing 2RDM into disk .......................... ");

    auto psio_ = _default_psio_lib_;

    IWL d2aa(psio_.get(), PSIF_MO_AA_TPDM, 1.0e-14, 0, 0);
    IWL d2ab(psio_.get(), PSIF_MO_AB_TPDM, 1.0e-14, 0, 0);
    IWL d2bb(psio_.get(), PSIF_MO_BB_TPDM, 1.0e-14, 0, 0);
  
    std::vector<size_t> core_all_ = mo_space_info_->get_absolute_mo("RESTRICTED_DOCC");
    std::vector<size_t> actv_all_ = mo_space_info_->get_absolute_mo("ACTIVE");
    // std::vector<size_t> virt_all_ = mo_space_info_->get_absolute_mo("RESTRICTED_UOCC");

    for(size_t i = 0, size_c = core_all_.size(); i < size_c; ++i) {
        for(size_t j = 0; j < size_c; ++j) {

            auto m = core_all_[i];
            auto n = core_all_[j];

            if (m!=n) {

                d2aa.write_value(m, m, n, n, 0.25, 0, "NULL", 0);
                d2bb.write_value(m, m, n, n, 0.25, 0, "NULL", 0);

                d2aa.write_value(m, n, n, m, -0.25, 0, "NULL", 0);
                d2bb.write_value(m, n, n, m, -0.25, 0, "NULL", 0);

            }
                
            d2ab.write_value(m, m, n, n, 1.00, 0, "NULL", 0);
        }
    }



    for(size_t i = 0, size_c = core_all_.size(), size_a = actv_all_.size(); i < size_a; ++i) {
        for(size_t j = 0; j < size_a; ++j) {

            auto idx = i * na_ + j;
            auto u = actv_all_[i];
            auto v = actv_all_[j];
            
            auto gamma_a = Gamma1_.block("aa").data()[idx];
            auto gamma_b = Gamma1_.block("AA").data()[idx];

            for(size_t k = 0; k < size_c; ++k) {

                auto m = core_all_[k];

                d2aa.write_value(v, u, m, m, 0.5 * gamma_a, 0, "NULL", 0);
                d2bb.write_value(v, u, m, m, 0.5 * gamma_b, 0, "NULL", 0);

                d2aa.write_value(v, m, m, u, -0.5 * gamma_a, 0, "NULL", 0);
                d2bb.write_value(v, m, m, u, -0.5 * gamma_b, 0, "NULL", 0);

                /// this need to be checked, inconsistency exists
                d2ab.write_value(v, u, m, m, (gamma_a + gamma_b), 0, "NULL", 0);

            }
        }
    }

    for(size_t i = 0, size_a = actv_all_.size(); i < size_a; ++i) {
        for(size_t j = 0; j < size_a; ++j) {
            for(size_t k = 0; k < size_a; ++k) {
                for(size_t l = 0; l < size_a; ++l) {

                    auto u = actv_all_[i];
                    auto v = actv_all_[j];
                    auto x = actv_all_[k];
                    auto y = actv_all_[l];
                    auto idx = i * na_ * na_ * na_ + j * na_ * na_ + k * na_ + l;
                    auto gamma_aa = Gamma2_.block("aaaa").data()[idx];
                    auto gamma_bb = Gamma2_.block("AAAA").data()[idx];
                    auto gamma_ab = Gamma2_.block("aAaA").data()[idx];

                    
                    d2aa.write_value(x, u, y, v, 0.25 * gamma_aa, 0, "NULL", 0);
                    d2bb.write_value(x, u, y, v, 0.25 * gamma_bb, 0, "NULL", 0);

                    d2ab.write_value(x, u, y, v, gamma_ab, 0, "NULL", 0);
                }
            }
        }
    }
  
    outfile->Printf("Done");

    d2aa.flush(1);
    d2bb.flush(1);
    d2ab.flush(1);

    d2aa.set_keep_flag(1);
    d2bb.set_keep_flag(1);
    d2ab.set_keep_flag(1);

    d2aa.close();
    d2bb.close();
    d2ab.close();
}


}



