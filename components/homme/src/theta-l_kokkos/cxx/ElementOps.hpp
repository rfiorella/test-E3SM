#ifndef HOMMEXX_ELEMENT_OPS_HPP
#define HOMMEXX_ELEMENT_OPS_HPP

#include "Types.hpp"
#include "KernelVariables.hpp"
#include "HybridVCoord.hpp"
#include "ColumnOps.hpp"
#include "PhysicalConstants.hpp"

namespace Homme {

class ElementOps {
public:
  ElementOps () = default;
  ~ElementOps () = default;

  void init (const HybridVCoord& hvcoord) {
    m_hvcoord = hvcoord;
  }

  template<typename InputProvider>
  KOKKOS_INLINE_FUNCTION
  void get_R_star (const KernelVariables& kv,
                   const bool use_moisture,
                   const InputProvider& Q,
                   const ExecViewUnmanaged<Scalar[NUM_LEV]>& R) const {
    if (use_moisture) {
      constexpr Real Rgas = PhysicalConstants::Rgas;
      constexpr Real Rwv  = PhysicalConstants::Rwater_vapor;
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        R(ilev) = (Rgas + (Rwv-Rgas)*Q(ilev));
      });
    } else {
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        R(ilev) = PhysicalConstants::Rgas;
      });
    }
  }

  KOKKOS_INLINE_FUNCTION
  void compute_hydrostatic_p (const KernelVariables& kv,
                              const ExecViewUnmanaged<const Scalar[NUM_LEV  ]>& dp,
                              const ExecViewUnmanaged<      Scalar[NUM_LEV_P]>& p_i,
                              const ExecViewUnmanaged<      Scalar[NUM_LEV  ]>& pi) const
  {
    p_i(0)[0] = m_hvcoord.hybrid_ai0*m_hvcoord.ps0;
    ColumnOps::column_scan_mid_to_int<true>(kv,dp,p_i);
#ifdef XX_NONBFB_COMING
    ColumnOps::compute_midpoint_values(kv,p_i,pi);
#else
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                         [&](const int ilev) {
      pi(ilev) = p_i(ilev) + dp(ilev)/2;
    });
#endif
  }

  template<typename InputProvider>
  KOKKOS_INLINE_FUNCTION
  void compute_theta_ref (const KernelVariables& kv,
                          const InputProvider& p,
                          const ExecViewUnmanaged<Scalar[NUM_LEV]>& theta_ref) const {
    assert (m_hvcoord.m_inited);
    // theta_ref = T0/exner + T1, with T0,T1 fixed
    // exner = (p/p0)^k
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                         [&](const int ilev) {
      // Compute exner, store in theta_ref
      // TODO: F90 does p(k) = (p_i(k)+p_i(k+1)) / (2*p0).
      //       If this is a non BFB source, incorporate p0 scaling
      //       in the calculation of p
      theta_ref(ilev) = pow(p(ilev)/PhysicalConstants::p0,PhysicalConstants::kappa);

      // Compute theta_ref
      theta_ref(ilev) = T0/theta_ref(ilev) + T1;
    });
  }

private:

  static constexpr Real TREF = 288.0;
  static constexpr Real T1 = 0.0065f*TREF*PhysicalConstants::cp/PhysicalConstants::g;
  static constexpr Real T0 = TREF-T1;

  HybridVCoord    m_hvcoord;
};

} // namespace Homme

#endif // HOMMEXX_ELEMENT_OPS_HPP
