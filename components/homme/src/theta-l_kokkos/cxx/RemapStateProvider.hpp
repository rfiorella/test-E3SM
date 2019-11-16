/********************************************************************************
 * HOMMEXX 1.0: Copyright of Sandia Corporation
 * This software is released under the BSD license
 * See the file 'COPYRIGHT' in the HOMMEXX/src/share/cxx directory
 *******************************************************************************/

#ifndef HOMMEXX_REMAP_STATE_PROVIDER_HPP
#define HOMMEXX_REMAP_STATE_PROVIDER_HPP

#include "Elements.hpp"
#include "EquationOfState.hpp"
#include "ElementOps.hpp"
#include "ErrorDefs.hpp"
#include "ColumnOps.hpp"
#include "Context.hpp"
#include "SimulationParams.hpp"
#include "HybridVCoord.hpp"
#include "KernelVariables.hpp"
#include "Types.hpp"
#include "utilities/SubviewUtils.hpp"

namespace Homme {
namespace Remap {

struct RemapStateProvider {

  EquationOfState   m_eos;
  ElementOps        m_elem_ops;
  ElementsState     m_state;
  ElementsGeometry  m_geometry;
  HybridVCoord      m_hvcoord;

  // These two morally are d(w_i)/ds and d(phinh_i)/ds.
  // However, since in the remap we need to multiply by ds
  // (the layer thickness, aka dp), we simply compute
  // d(w_i) and d(phinh_i).
  ExecViewManaged<Scalar*  [NP][NP][NUM_LEV]> m_delta_w;
  ExecViewManaged<Scalar*  [NP][NP][NUM_LEV]> m_delta_phinh;

  ExecViewManaged<Scalar*  [NP][NP][NUM_LEV_P]> m_temp;

  explicit RemapStateProvider(const Elements& elements)
   : m_state(elements.m_state)
   , m_geometry(elements.m_geometry)
   , m_delta_w ("w_i increments",elements.num_elems())
   , m_delta_phinh ("phinh_i increments",elements.num_elems())
  {
    // Fetch SimulationParams and HybridVCoord from the context
    const auto& params = Context::singleton().get<SimulationParams>();
    assert (params.params_set);

    m_hvcoord = Context::singleton().get<HybridVCoord>();
    assert (m_hvcoord.m_inited);

    m_eos.init(params.theta_hydrostatic_mode,m_hvcoord);
    m_elem_ops.init(m_hvcoord);
  }

  // TODO: find a way to get the temporary from the FunctorsBuffersManager class
  template<typename Tag>
  void allocate_buffers(const Kokkos::TeamPolicy<ExecSpace,Tag>& policy) {
    const int nteams = Homme::get_num_concurrent_teams(policy);
    m_temp = decltype(m_temp)("temporary",nteams);
  }

  KOKKOS_INLINE_FUNCTION
  int num_states_remap() const { return 5;}

  KOKKOS_INLINE_FUNCTION
  int num_states_preprocess() const { return 2;}

  KOKKOS_INLINE_FUNCTION
  int num_states_postprocess() const { return 2;}

  KOKKOS_INLINE_FUNCTION
  bool is_intrinsic_state (const int istate) const {
    assert (istate>=0 && istate<num_states_remap());

    if (istate==3 || istate==4) {
      // Horizontal velocity needs to be rescaled by dp
      return true;
    }

    // Other quantities are already scaled by dp
    return false;
  }

  KOKKOS_INLINE_FUNCTION
  void preprocess_state (const KernelVariables& kv,
                         const int istate,
                         const int np1,
                         ExecViewUnmanaged<const Scalar[NP][NP][NUM_LEV]> dp) const {
    if (istate==0) {
      // Compute delta_w
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP;
        const int jgp = idx % NP;
        auto w_i = Homme::subview(m_state.m_w_i,kv.ie,np1,igp,jgp);
        auto delta_w = Homme::subview(m_delta_w,kv.ie,igp,jgp);

        ColumnOps::compute_midpoint_delta(kv,w_i,delta_w);
      });
    } else if (istate==1) {
      // Compute delta_phinh
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP;
        const int jgp = idx % NP;

        // Remove hydrostatic phi before remap. Use still unused delta_phinh to store p.
        // Recycle m_temp for both p_i and phi_i_ref
        auto dp_pt = Homme::subview(dp,igp,jgp);
        auto phinh_i = Homme::subview(m_state.m_phinh_i,kv.ie,np1,igp,jgp);
        auto delta_phinh = Homme::subview(m_delta_phinh,kv.ie,igp,jgp);
        auto& p = delta_phinh;
        auto p_i = Homme::subview(m_temp,kv.team_idx,igp,jgp);
        auto& phi_ref = p_i;

        m_elem_ops.compute_hydrostatic_p(kv,dp_pt,p_i,delta_phinh);
        m_eos.compute_phi_i(kv,m_geometry.m_phis(kv.ie,igp,jgp),
                            Homme::subview(m_state.m_vtheta_dp,kv.ie,np1,igp,jgp),
                            p,
                            phi_ref);

        Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV_P),
                             [&](const int ilev) {
          phinh_i(ilev) -= phi_ref(ilev);
        });

        // Now compute phinh_i increments
        ColumnOps::compute_midpoint_delta(kv,phinh_i,delta_phinh);
      });
    }
  }

  KOKKOS_INLINE_FUNCTION
  void postprocess_state (const KernelVariables& kv,
                          const int istate,
                          const int np1,
                          ExecViewUnmanaged<const Scalar[NP][NP][NUM_LEV]> dp) const {
    using InfoI = ColInfo<NUM_INTERFACE_LEV>;
    using InfoM = ColInfo<NUM_PHYSICAL_LEV>;
    if (istate==0) {
      // Update w_i
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP;
        const int jgp = idx % NP;
        auto w_i = Homme::subview(m_state.m_w_i,kv.ie,np1,igp,jgp);
        auto delta_w = Homme::subview(m_delta_w,kv.ie,igp,jgp);

        // w_i(k) = w_i(k+1) - delta_w(k), so do a backward scan sum of -delta_w
        auto minus_delta = [&](const int ilev)->Scalar { return -delta_w(ilev); };
        ColumnOps::column_scan_mid_to_int<false>(kv,minus_delta,w_i);

        // Since u changed, update w_i b.c. at the surface
        Kokkos::single(Kokkos::PerThread(kv.team),[&](){
          constexpr int LAST_MID_PACK     = InfoM::LastPack;
          constexpr int LAST_MID_PACK_END = InfoM::LastPackEnd;
          constexpr int LAST_INT_PACK     = InfoI::LastPack;
          constexpr int LAST_INT_PACK_END = InfoI::LastPackEnd;
          constexpr auto g = PhysicalConstants::g;

          const auto gradphis = Homme::subview(m_geometry.m_gradphis,kv.ie);
          const auto v        = Homme::subview(m_state.m_v,kv.ie,np1);

          w_i(LAST_INT_PACK)[LAST_INT_PACK_END] = 
                (v(0,igp,jgp,LAST_MID_PACK)[LAST_MID_PACK_END]*gradphis(0,igp,jgp) +
                 v(1,igp,jgp,LAST_MID_PACK)[LAST_MID_PACK_END]*gradphis(1,igp,jgp)) / g;
        });
      });
    } else if (istate==1) {
      // Update phinh_i
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP;
        const int jgp = idx % NP;
        // Recycle m_temp for both p_i and phi_i_ref
        auto phinh_i = Homme::subview(m_state.m_phinh_i,kv.ie,np1,igp,jgp);
        auto delta_phinh = Homme::subview(m_delta_phinh,kv.ie,igp,jgp);
        auto dp_pt = Homme::subview(dp,igp,jgp);
        auto& p = delta_phinh;
        auto p_i = Homme::subview(m_temp,kv.team_idx,igp,jgp);
        auto& phi_ref = p_i;

        // phinh_i(k) = phinh_i(k+1) - delta_phinh(k), so do a backward scan sum of -delta_phinh
        auto minus_delta = [&](const int ilev)->Scalar { return -delta_phinh(ilev); };
        ColumnOps::column_scan_mid_to_int<false>(kv,minus_delta,phinh_i);

        // Need to add back the reference phi. Recycle delta_phinh to store p.
        // Recycle m_temp for both p_i and phi_i_ref
        m_elem_ops.compute_hydrostatic_p(kv,dp_pt,p_i,p);
        m_eos.compute_phi_i(kv,m_geometry.m_phis(kv.ie,igp,jgp),
                            Homme::subview(m_state.m_vtheta_dp,kv.ie,np1,igp,jgp),
                            p,
                            phi_ref);

        Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV_P),
                             [&](const int ilev) {
          phinh_i(ilev) += phi_ref(ilev);
        });
      });
    }
  }

  KOKKOS_INLINE_FUNCTION
  ExecViewUnmanaged<Scalar[NP][NP][NUM_LEV]>
  get_state(const KernelVariables &kv, int np1, int var) const {
    assert(var>=0 && var<=4);
    switch (var) {
    case 0:
      return Homme::subview(m_delta_w, kv.ie);
    case 1:
      return Homme::subview(m_delta_phinh, kv.ie);
    case 2:
      return Homme::subview(m_state.m_vtheta_dp, kv.ie, np1);
    case 3:
      return Homme::subview(m_state.m_v, kv.ie, np1, 0);
    case 4:
      return Homme::subview(m_state.m_v, kv.ie, np1, 1);
    default:
      Kokkos::abort("RemapStateProvider: invalid variable index.\n");
      return ExecViewUnmanaged<Scalar[NP][NP][NUM_LEV]>();
    }
  }
};

} // namespace Remap
} // namespace Homme

#endif // HOMMEXX_REMAP_STATE_PROVIDER_HPP
