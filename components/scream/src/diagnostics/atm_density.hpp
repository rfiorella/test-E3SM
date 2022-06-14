#ifndef EAMXX_ATM_DENSITY_DIAGNOSTIC_HPP
#define EAMXX_ATM_DENSITY_DIAGNOSTIC_HPP

#include "share/atm_process/atmosphere_diagnostic.hpp"
#include "share/util/scream_common_physics_functions.hpp"

namespace scream
{

/*
 * This diagnostic will produce the potential temperature.
 */

class AtmDensityDiagnostic : public AtmosphereDiagnostic
{
public:
  template <typename S>
  using SmallPack     = ekat::Pack<S,SCREAM_SMALL_PACK_SIZE>;

  using Spack         = SmallPack<Real>;
  using Pack          = ekat::Pack<Real,Spack::n>;
  using PF            = scream::PhysicsFunctions<DefaultDevice>;
  using KT            = KokkosTypes<DefaultDevice>;
  using view_2d       = typename KT::template view_2d<Spack>;
  using view_2d_const = typename KT::template view_2d<const Spack>;

  // Constructors
  AtmDensityDiagnostic (const ekat::Comm& comm, const ekat::ParameterList& params);

  // Set type to diagnostic
  AtmosphereProcessType type () const { return AtmosphereProcessType::Diagnostic; }

  // The name of the diagnostic
  std::string name () const { return "Atmosphere Density"; } 

  // Get the required grid for the diagnostic
  std::set<std::string> get_required_grids () const {
    static std::set<std::string> s;
    s.insert(m_params.get<std::string>("Grid"));
    return s;
  }

  // Set the grid
  void set_grids (const std::shared_ptr<const GridsManager> grids_manager);

  // Actual diagnostic calculation 
  struct run_diagnostic_impl {
    run_diagnostic_impl() = default;
    // Functor for Kokkos loop
    KOKKOS_INLINE_FUNCTION
    void operator() (const int& idx) const {
      const int icol  = idx / m_npack;
      const int jpack = idx % m_npack;
      const Spack& T_mid_ij(T_mid(icol,jpack));
      const Spack& p_mid_ij(p_mid(icol,jpack));
      const Spack& qv_mid_ij(qv_mid(icol,jpack));
      const Spack& pseudo_mid_ij(pseudo_density_mid(icol,jpack));

      auto dz_ij = PF::calculate_dz(pseudo_mid_ij,p_mid_ij,T_mid_ij,qv_mid_ij);
      output(icol,jpack) = PF::calculate_density(pseudo_mid_ij,dz_ij);
    }
    int m_ncol, m_npack;
    view_2d_const p_mid;
    view_2d_const T_mid;
    view_2d_const qv_mid;
    view_2d_const pseudo_density_mid;
    view_2d       output;
    // assign variables to this structure
    void set_variables(const int ncol, const int npack,
      const view_2d_const& pmid_, const view_2d_const& tmid_, 
      const view_2d_const& qvmid_, const view_2d_const& pseudo_density_mid_, 
      const view_2d& output_)
    {
      m_ncol = ncol;
      m_npack = npack;
      // IN
      T_mid  = tmid_;
      p_mid  = pmid_;
      qv_mid = qvmid_;
      pseudo_density_mid = pseudo_density_mid_;
      // OUT
      output = output_;
    }
  }; // struct run_diagnostic_impl

protected:

  // The three main overrides for the subcomponent
  void initialize_impl (const RunType run_type);
  void run_impl        (const int dt);
  void finalize_impl   ();

  // Keep track of field dimensions
  Int m_num_cols; 
  Int m_num_levs;

  // Structure to run the diagnostic
  run_diagnostic_impl  run_diagnostic;

}; // class AtmDensityDiagnostic

} //namespace scream

#endif // EAMXX_ATM_DENSITY_DIAGNOSTIC_HPP
