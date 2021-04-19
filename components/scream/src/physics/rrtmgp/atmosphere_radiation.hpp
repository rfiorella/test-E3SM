#ifndef SCREAM_RRTMGP_RADIATION_HPP
#define SCREAM_RRTMGP_RADIATION_HPP

#include "share/atm_process/atmosphere_process.hpp"
#include "ekat/ekat_parameter_list.hpp"
#include <string>

namespace scream {
/*
 * Class responsible for atmosphere radiative transfer. The AD should store
 * exactly ONE instance of this class in its list of subcomponents.
 */

class RRTMGPRadiation : public AtmosphereProcess {
public:
  using field_type       = Field<      Real>;
  using const_field_type = Field<const Real>;

  // Constructors
  RRTMGPRadiation (const ekat::Comm& comm, const ekat::ParameterList& params);

  // The type of the subcomponent
  AtmosphereProcessType type () const { return AtmosphereProcessType::Physics; }

  // The name of the subcomponent
  std::string name () const { return "Radiation"; }

  // The communicator used by the subcomponent
  const ekat::Comm& get_comm () const { return m_rrtmgp_comm; }

  // Required grid for the subcomponent (??)
  std::set<std::string> get_required_grids () const {
      static std::set<std::string> s;
      s.insert(m_rrtmgp_params.get<std::string>("Grid"));
      return s;
  }

  // Set the grid
  void set_grids (const std::shared_ptr<const GridsManager> grid_manager);

// NOTE: cannot use lambda functions for CUDA devices if these are protected!
public:
  // The three main interfaces for the subcomponent
  void initialize_impl (const util::TimeStamp& t0);
  void run_impl        (const Real dt);
  void finalize_impl   ();

  // Set fields in the atmosphere process
  void set_required_field_impl (const Field<const Real>& f);
  void set_computed_field_impl (const Field<      Real>& f);

  // Input and input/output fields
  std::map<std::string,const_field_type> m_rrtmgp_fields_in;
  std::map<std::string,field_type>       m_rrtmgp_fields_out;

  template<typename T>
  using view_type = field_type::view_type<T*>;

  template<typename T>
  using host_view_type = field_type::get_view_type<view_type<T>,Host>;

  using host_view_in_type   = host_view_type<const_field_type::RT>;
  using host_view_out_type  = host_view_type<      field_type::RT>;
  std::map<std::string,host_view_in_type>   m_rrtmgp_host_views_in;
  std::map<std::string,host_view_out_type>  m_rrtmgp_host_views_out;


  std::map<std::string,const Real*>  m_raw_ptrs_in;
  std::map<std::string,Real*>        m_raw_ptrs_out;

  util::TimeStamp m_current_ts;
  ekat::Comm            m_rrtmgp_comm;
  ekat::ParameterList   m_rrtmgp_params;

  // Keep track of number of columns and levels
  int m_ncol;
  int m_nlay;

  // Need to hard-code some dimension sizes for now. 
  // TODO: find a better way of configuring this
  const int m_nswbands = 14;
  const int m_nlwbands = 16;

  // These are the gases that we keep track of
  const int m_ngas = 8;
  const std::string m_gas_names[8] = {
      "h2o", "co2", "o3", "n2o",
      "co" , "ch4", "o2", "n2"
  };

private: 
    void require_unpadded(const Field<const Real>& f);

};  // class RRTMGPRadiation

}  // namespace scream

#endif  // SCREAM_RRTMGP_RADIATION_HPP
