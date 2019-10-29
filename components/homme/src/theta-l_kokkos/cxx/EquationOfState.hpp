#ifndef HOMMEXX_EQUATION_OF_STATE_HPP
#define HOMMEXX_EQUATION_OF_STATE_HPP

#include "Types.hpp"
#include "ColumnOps.hpp"
#include "HybridVCoord.hpp"
#include "KernelVariables.hpp"
#include "PhysicalConstants.hpp"

#include "utilities/VectorUtils.hpp"
#include "utilities/ViewUtils.hpp"

namespace Homme {

class EquationOfState {
public:

  using MIDPOINTS = ColInfo<NUM_PHYSICAL_LEV>;
  using INTERFACES = ColInfo<NUM_INTERFACE_LEV>;

  EquationOfState () = default;

  void init (const bool theta_hydrostatic_mode,
             const HybridVCoord& hvcoord) {
    m_theta_hydrostatic_mode = theta_hydrostatic_mode;
    m_hvcoord = hvcoord;
    assert (m_hvcoord.m_inited);
  }

  KOKKOS_INLINE_FUNCTION
  void compute_hydrostatic_p (const KernelVariables& kv,
                              const ExecViewUnmanaged<const Scalar[NUM_LEV  ]>& dp,
                              const ExecViewUnmanaged<      Scalar[NUM_LEV_P]>& p_i,
                              const ExecViewUnmanaged<      Scalar[NUM_LEV  ]>& pi) const
  {
    // If you're not hydrostatic, check outside the function
    assert (m_theta_hydrostatic_mode);
    p_i(0)[0] = m_hvcoord.hybrid_ai0*m_hvcoord.ps0;
    m_col_ops.column_scan_mid_to_int<true>(kv,dp,p_i);
    m_col_ops.compute_midpoint_values(kv,p_i,pi);
  }

  KOKKOS_INLINE_FUNCTION
  void compute_exner (const KernelVariables& kv,
                      const ExecViewUnmanaged<const Scalar[NUM_LEV]>& pi,
                      const ExecViewUnmanaged<      Scalar[NUM_LEV]>& exner) const
  {
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                         [&](const int ilev) {
      // Avoid temporaries
      exner(ilev) = pi(ilev);
      exner(ilev) /= PhysicalConstants::p0;
      exner(ilev) = pow(exner(ilev),PhysicalConstants::kappa);
    });
  }

  template<typename VThetaProvider, typename PhiProvider>
  KOKKOS_INLINE_FUNCTION
  void compute_pnh_and_exner (const KernelVariables& kv,
                              const VThetaProvider& vtheta_dp,
                              const PhiProvider&    phi_i,
                              const ExecViewUnmanaged<Scalar[NUM_LEV  ]>& pnh,
                              const ExecViewUnmanaged<Scalar[NUM_LEV  ]>& exner) const
  {
    // If you're hydrostatic, check outside the function
    assert (!m_theta_hydrostatic_mode);
    // Compute:
    //  1) p_over_exner = -Rgas*vtheta_dp/delta(phi_i)
    //  2) pnh = p0 (p_over_exner/p0)^(1/(1-kappa))
    //  3) exner = pnh/p_over_exner

    // To avoid temporaries, use exner to store some temporaries
    m_col_ops.compute_midpoint_delta(kv,phi_i,exner);

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                         [&](const int ilev) {

      // TODO: should do *= Rgas/p0, but would lose BFB with F90.
      exner(ilev) = (-PhysicalConstants::Rgas)*vtheta_dp(ilev) / exner(ilev);
      pnh(ilev) = exner(ilev)/PhysicalConstants::p0;
      pnh(ilev) = pow(pnh(ilev),1.0/(1.0-PhysicalConstants::kappa));
      pnh(ilev) *= PhysicalConstants::p0;

      exner(ilev) = pnh(ilev)/exner(ilev);
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_dpnh_dp_i (const KernelVariables& kv,
                          const ExecViewUnmanaged<const Scalar[NUM_LEV  ]>& pnh,
                          const ExecViewUnmanaged<const Scalar[NUM_LEV_P]>& dp_i,
                          const ExecViewUnmanaged<      Scalar[NUM_LEV_P]>& dpnh_dp_i) const
  {
    if (m_theta_hydrostatic_mode) {
      // Set dpnh_dp_i to 1.0
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV_P),
                           [&](const int ilev) {
        dpnh_dp_i(ilev) = 1.0;
      });
    } else {
      // Start with dpnh_dp_i = delta(pnh)/dp_i. Skip bc's, cause we do our own here
      m_col_ops.compute_interface_delta<CombineMode::Replace,BCType::DoNothing>(kv.team,pnh,dpnh_dp_i);

      // Note: top and bottom need special treatment, so we may as well stop at NUM_LEV here (rather than NUM_LEV_P)
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        dpnh_dp_i(ilev) /= dp_i(ilev);
      });

      // Boundaries: delta(x) = 2*(x_m(last)-x_i(last)).
      // Top: pnh_i = pi_i = hyai(0)*ps0.
      // Bottom: approximate with hydrostatic, so that dpnh_dp_i=1
      dpnh_dp_i(0)[0] = 2*(pnh(0)[0] - m_hvcoord.hybrid_ai(0)*m_hvcoord.ps0)/dp_i(0)[0];
      const Real pnh_last = pnh(MIDPOINTS::LastPack)[MIDPOINTS::LastVecEnd];
      const Real dp_last = dp_i(INTERFACES::LastPack)[INTERFACES::LastVecEnd];
      const Real pnh_i_last = pnh_last + dp_last/2;
      dpnh_dp_i(INTERFACES::LastPack)[INTERFACES::LastVecEnd] = 2*(pnh_i_last - pnh_last)/dp_last;
    }
  }

  // Note: if p is hydrostatic, this will compute the hydrostatic geopotential,
  //       otherwise it will be the non-hydrostatic. In particular, if the pressure
  //       p is computed using dp from pnh, this will be the discrete inverse of
  //       the compute_pnh_and_exner method.
  KOKKOS_INLINE_FUNCTION
  void compute_phi_i (const KernelVariables& kv,
                      const ExecViewUnmanaged<const Real   [NP][NP]           >& phis,
                      const ExecViewUnmanaged<const Scalar [NP][NP][NUM_LEV]  >& vtheta_dp,
                      const ExecViewUnmanaged<const Scalar [NP][NP][NUM_LEV]  >& p,
                      const ExecViewUnmanaged<      Scalar [NP][NP][NUM_LEV_P]>& phi_i) const {
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;
      compute_phi_i(kv, phis(igp,jgp),
                    Homme::subview(vtheta_dp,igp,jgp),
                    Homme::subview(p,igp,jgp),
                    Homme::subview(phi_i,igp,jgp));
    });
  }

  // VThetaProvider can be either a 1d view or a lambda,
  // as long as vtheta_dp(ilev) returns vtheta_dp at pack ilev
  template<typename VThetaProvider>
  KOKKOS_INLINE_FUNCTION
  void compute_phi_i (const KernelVariables& kv, const Real phis,
                      const VThetaProvider& vtheta_dp,
                      const ExecViewUnmanaged<const Scalar [NUM_LEV]  >& p,
                      const ExecViewUnmanaged<      Scalar [NUM_LEV_P]>& phi_i) const
  {
    // Init phi on surface with phis
    phi_i(INTERFACES::LastPack)[INTERFACES::LastVecEnd] = phis;

    // Use ColumnOps to do the scan sum
    auto integrand_provider = [&](const int ilev)->Scalar {
      constexpr Real p0    = PhysicalConstants::p0;
      constexpr Real kappa = PhysicalConstants::kappa;
      constexpr Real Rgas  = PhysicalConstants::Rgas;
      // TODO: remove temporaries
      return (Rgas*vtheta_dp(ilev) * pow(p(ilev)/p0,kappa-1)) / p0;
    };

    m_col_ops.column_scan_mid_to_int<false>(kv,integrand_provider,phi_i);
  }

  // If exner is available, then use exner/p instead of (p/p0)^(k-1)/p0, to avoid dealing with exponentials
  // VThetaProvider can be either a 1d view or a lambda,
  // as long as vtheta_dp(ilev) returns vtheta_dp at pack ilev
  template<typename VThetaProvider>
  KOKKOS_INLINE_FUNCTION
  void compute_phi_i (const KernelVariables& kv,
                      const ExecViewUnmanaged<const Real   [NP][NP]           >& phis,
                      const VThetaProvider& vtheta_dp,
                      const ExecViewUnmanaged<const Scalar [NP][NP][NUM_LEV]  >& p,
                      const ExecViewUnmanaged<const Scalar [NP][NP][NUM_LEV]  >& exner,
                      const ExecViewUnmanaged<      Scalar [NP][NP][NUM_LEV_P]>& phi_i) const {
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;
      compute_phi_i(kv, phis(igp,jgp),
                    Homme::subview(vtheta_dp,igp,jgp),
                    Homme::subview(p,igp,jgp),
                    Homme::subview(exner,igp,jgp),
                    Homme::subview(phi_i,igp,jgp));
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_phi_i (const KernelVariables& kv, const Real phis,
                      const ExecViewUnmanaged<const Scalar [NUM_LEV]  >& vtheta_dp,
                      const ExecViewUnmanaged<const Scalar [NUM_LEV]  >& p,
                      const ExecViewUnmanaged<const Scalar [NUM_LEV]  >& exner,
                      const ExecViewUnmanaged<      Scalar [NUM_LEV_P]>& phi_i) const
  {
    // Init phi on surface with phis
    phi_i(INTERFACES::LastPack)[INTERFACES::LastVecEnd] = phis;

    // Use ColumnOps to do the scan sum
    auto integrand_provider = [&](const int ilev)->Scalar {
      constexpr Real Rgas  = PhysicalConstants::Rgas;
      return Rgas*vtheta_dp(ilev)*exner(ilev)/p(ilev);
    };

    m_col_ops.column_scan_mid_to_int<false>(kv,integrand_provider,phi_i);
  }

private:

  bool            m_theta_hydrostatic_mode;
  ColumnOps       m_col_ops;
  HybridVCoord    m_hvcoord;
};

} // namespace Homme

#endif // HOMMEXX_EQUATION_OF_STATE_HPP
