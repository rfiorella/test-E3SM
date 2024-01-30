#include <physics/mam/eamxx_mam_optics_process_interface.hpp>
#include <share/property_checks/field_lower_bound_check.hpp>
#include <share/property_checks/field_within_interval_check.hpp>

#include "scream_config.h" // for SCREAM_CIME_BUILD
#include <ekat/ekat_assert.hpp>

#include "share/io/scorpio_input.hpp"
#include "share/grid/point_grid.hpp"

namespace scream
{

MAMOptics::MAMOptics(
    const ekat::Comm& comm,
    const ekat::ParameterList& params)
  : AtmosphereProcess(comm, params),
    aero_config_() {
}

AtmosphereProcessType MAMOptics::type() const {
  return AtmosphereProcessType::Physics;
}

std::string MAMOptics::name() const {
  return "mam4_optics";
}

void MAMOptics::set_grids(const std::shared_ptr<const GridsManager> grids_manager) {
  using namespace ekat::units;
  constexpr int nvars = mam4::ndrop::nvars;

  grid_ = grids_manager->get_grid("Physics");
  const auto& grid_name = grid_->name();
  auto q_unit = kg/kg; // mass mixing ratios [kg stuff / kg air]
  q_unit.set_string("kg/kg");
  auto n_unit = 1/kg;  // number mixing ratios [# / kg air]
  n_unit.set_string("#/kg");
  // Units nondim(0,0,0,0,0,0,0);
  const auto m2 = m*m;
  const auto s2 = s*s;

  ncol_ = grid_->get_num_local_dofs();      // number of columns on this rank
  nlev_ = grid_->get_num_vertical_levels(); // number of levels per column
  nswbands_ = mam4::modal_aer_opt::nswbands; // number of shortwave bands
  nlwbands_ = mam4::modal_aer_opt::nlwbands; // number of longwave bands

  // Define the different field layouts that will be used for this process
  using namespace ShortFieldTagsNames;

  // Define aerosol optics fields computed by this process.
  auto nondim = Units::nondimensional();
  FieldLayout scalar3d_swbandp_layout { {COL, SWBND, ILEV}, {ncol_, nswbands_, nlev_+1} };
  FieldLayout scalar3d_lwband_layout { {COL, LWBND, LEV}, {ncol_, nlwbands_, nlev_} };
  FieldLayout scalar3d_layout_int{ {COL, ILEV}, {ncol_, nlev_+1} };

  // layout for 2D (1d horiz X 1d vertical) variable
  FieldLayout scalar2d_layout_col{ {COL}, {ncol_} };

  // layout for 3D (2d horiz X 1d vertical) variables
  FieldLayout scalar3d_layout_mid{ {COL, LEV}, {ncol_, nlev_} };
  add_field<Required>("omega", scalar3d_layout_mid, Pa/s, grid_name); // vertical pressure velocity
  add_field<Required>("T_mid", scalar3d_layout_mid, K, grid_name); // Temperature
  add_field<Required>("p_mid", scalar3d_layout_mid, Pa, grid_name); // total pressure

  // add_field<Required>("z_int", scalar3d_layout_int, m, grid_name); // vertical position at interface
  // add_field<Required>("z_mid", scalar3d_layout_mid, m, grid_name); // vertical position pressure
  add_field<Required>("p_int", scalar3d_layout_int, Pa, grid_name); // total pressure
  add_field<Required>("pseudo_density", scalar3d_layout_mid, Pa, grid_name);
  add_field<Required>("pseudo_density_dry", scalar3d_layout_mid, Pa, grid_name);

  add_field<Required>("qv", scalar3d_layout_mid, q_unit, grid_name, "tracers"); // specific humidity
  add_field<Required>("qi", scalar3d_layout_mid, q_unit, grid_name, "tracers"); // ice wet mixing ratio
  add_field<Required>("ni", scalar3d_layout_mid, n_unit, grid_name, "tracers"); // ice number mixing ratio

   // droplet activation can alter cloud liquid and number mixing ratios
  add_field<Updated>("qc", scalar3d_layout_mid, q_unit, grid_name, "tracers"); // cloud liquid wet mixing ratio
  add_field<Updated>("nc", scalar3d_layout_mid, n_unit, grid_name, "tracers"); // cloud liquid wet number mixing ratio

  add_field<Required>("phis",           scalar2d_layout_col, m2/s2, grid_name);
  add_field<Required>("cldfrac_tot", scalar3d_layout_mid, nondim, grid_name); // cloud fraction
  add_field<Required>("pbl_height", scalar2d_layout_col, m, grid_name); // planetary boundary layer height

  // add_field<Required>("cldn", scalar3d_layout_mid, nondim, grid_name); // could fraction

  // shortwave aerosol scattering asymmetry parameter [-]
  add_field<Computed>("aero_g_sw",   scalar3d_swbandp_layout, nondim, grid_name);
  // shortwave aerosol single-scattering albedo [-]
  add_field<Computed>("aero_ssa_sw", scalar3d_swbandp_layout, nondim, grid_name);
  // shortwave aerosol optical depth [-]
  add_field<Computed>("aero_tau_sw", scalar3d_swbandp_layout, nondim, grid_name);
  // longwave aerosol optical depth [-]
  add_field<Computed>("aero_tau_lw", scalar3d_lwband_layout, nondim, grid_name);
  // // aerosol extinction optical depth
  add_field<Computed>("aero_tau_forward", scalar3d_swbandp_layout, nondim, grid_name);

    // FIXME: this field doesn't belong here, but this is a convenient place to
  // FIXME: put it for now.
  // number mixing ratio for CCN
  using Spack      = ekat::Pack<Real,SCREAM_SMALL_PACK_SIZE>;
  using Pack       = ekat::Pack<Real,Spack::n>;
  constexpr int ps = Pack::n;
  // FieldLayout scalar3d_layout_mid { {COL, LEV}, {ncol_, nlev_} };
  add_field<Computed>("nccn", scalar3d_layout_mid, 1/kg, grid_name, ps);

  // (interstitial) aerosol tracers of interest: mass (q) and number (n) mixing ratios
  for (int m = 0; m < mam_coupling::num_aero_modes(); ++m) {
    const char* int_nmr_field_name = mam_coupling::int_aero_nmr_field_name(m);
    // printf("%s \n", int_nmr_field_name);

    add_field<Updated>(int_nmr_field_name, scalar3d_layout_mid, n_unit, grid_name, "tracers");
    for (int a = 0; a < mam_coupling::num_aero_species(); ++a) {
      const char* int_mmr_field_name = mam_coupling::int_aero_mmr_field_name(m, a);

      if (strlen(int_mmr_field_name) > 0) {
        add_field<Updated>(int_mmr_field_name, scalar3d_layout_mid, q_unit, grid_name, "tracers");
      }
    }
  }
// (cloud) aerosol tracers of interest: mass (q) and number (n) mixing ratios
 for (int m = 0; m < mam_coupling::num_aero_modes(); ++m) {
    const char* cld_nmr_field_name = mam_coupling::cld_aero_nmr_field_name(m);
    // printf("%s \n", int_nmr_field_name);

    add_field<Updated>(cld_nmr_field_name, scalar3d_layout_mid, n_unit, grid_name, "tracers");
    for (int a = 0; a < mam_coupling::num_aero_species(); ++a) {
      const char* cld_mmr_field_name = mam_coupling::cld_aero_mmr_field_name(m, a);

      if (strlen(cld_mmr_field_name) > 0) {
        add_field<Updated>(cld_mmr_field_name, scalar3d_layout_mid, q_unit, grid_name, "tracers");
      }
    }
  }

  // aerosol-related gases: mass mixing ratios
  for (int g = 0; g < mam_coupling::num_aero_gases(); ++g) {
    const char* gas_mmr_field_name = mam_coupling::gas_mmr_field_name(g);
    add_field<Updated>(gas_mmr_field_name, scalar3d_layout_mid, q_unit, grid_name, "tracers");
  }

}

size_t MAMOptics::requested_buffer_size_in_bytes() const
{
  return mam_coupling::buffer_size(ncol_, nlev_);
}

void MAMOptics::init_buffers(const ATMBufferManager &buffer_manager) {

  EKAT_REQUIRE_MSG(buffer_manager.allocated_bytes() >= requested_buffer_size_in_bytes(),
                   "Error! Insufficient buffer size.\n");

  size_t used_mem = mam_coupling::init_buffer(buffer_manager, ncol_, nlev_, buffer_);
  EKAT_REQUIRE_MSG(used_mem==requested_buffer_size_in_bytes(),
                   "Error! Used memory != requested memory for MAMMOptics.");
}


void MAMOptics::initialize_impl(const RunType run_type) {

    // populate the wet and dry atmosphere states with views from fields and
  // the buffer
  wet_atm_.qv = get_field_in("qv").get_view<const Real**>();
  wet_atm_.qc = get_field_out("qc").get_view<Real**>();
  wet_atm_.nc = get_field_out("nc").get_view<Real**>();
  wet_atm_.qi = get_field_in("qi").get_view<const Real**>();
  wet_atm_.ni = get_field_in("ni").get_view<const Real**>();
  wet_atm_.omega = get_field_in("omega").get_view<const Real**>();

    // FIXME: we have nvars in several process.
  constexpr int ntot_amode = mam4::AeroConfig::num_modes();

  dry_atm_.T_mid     = get_field_in("T_mid").get_view<const Real**>();
  dry_atm_.p_mid     = get_field_in("p_mid").get_view<const Real**>();
  p_int_     = get_field_in("p_int").get_view<const Real**>();
  // dry_atm_.p_del     = get_field_in("pseudo_density").get_view<const Real**>();
    // FIXME: In the nc file, there is also pseudo_density_dry
  dry_atm_.p_del     = get_field_in("pseudo_density_dry").get_view<const Real**>();
  // FIXME: is this a duplicate?
   p_del_     = get_field_in("pseudo_density").get_view<const Real**>();
  dry_atm_.cldfrac   = get_field_in("cldfrac_tot").get_view<const Real**>(); // FIXME: tot or liq?
  // FIXME: use this one instead
  // dry_atm_.cldfrac   = get_field_in("cldn").get_view<const Real**>();
  dry_atm_.pblh      = get_field_in("pbl_height").get_view<const Real*>();
  // FIXME:
  //  dry_atm_.phis      =  mam_coupling::view_1d("phis", ncol_);
  dry_atm_.phis      = get_field_in("phis").get_view<const Real*>();
  dry_atm_.z_mid     = buffer_.z_mid;
  dry_atm_.dz        = buffer_.dz;
  dry_atm_.z_iface   = buffer_.z_iface;
  dry_atm_.qv        = buffer_.qv_dry;
  dry_atm_.qc        = buffer_.qc_dry;
  dry_atm_.nc        = buffer_.nc_dry;
  dry_atm_.qi        = buffer_.qi_dry;
  dry_atm_.ni        = buffer_.ni_dry;
  dry_atm_.w_updraft = buffer_.w_updraft;
  dry_atm_.z_surf = 0.0; // FIXME: for now

  // FIXME: are we assuming constant aerosol between columns ?
  // set wet/dry aerosol state data (interstitial aerosols only)
  for (int m = 0; m < mam_coupling::num_aero_modes(); ++m) {
    const char* int_nmr_field_name = mam_coupling::int_aero_nmr_field_name(m);
    wet_aero_.int_aero_nmr[m] = get_field_out(int_nmr_field_name).get_view<Real**>();
    dry_aero_.int_aero_nmr[m] = buffer_.dry_int_aero_nmr[m];
    for (int a = 0; a < mam_coupling::num_aero_species(); ++a) {
      const char* int_mmr_field_name = mam_coupling::int_aero_mmr_field_name(m, a);
      if (strlen(int_mmr_field_name) > 0) {
        wet_aero_.int_aero_mmr[m][a] = get_field_out(int_mmr_field_name).get_view<Real**>();
        dry_aero_.int_aero_mmr[m][a] = buffer_.dry_int_aero_mmr[m][a];
      }
    }
  }

  // set wet/dry aerosol state data (cloud aerosols only)
  for (int m = 0; m < mam_coupling::num_aero_modes(); ++m) {
    const char* cld_nmr_field_name = mam_coupling::cld_aero_nmr_field_name(m);
    wet_aero_.cld_aero_nmr[m] = get_field_out(cld_nmr_field_name).get_view<Real**>();
    dry_aero_.cld_aero_nmr[m] = buffer_.dry_cld_aero_nmr[m];
    for (int a = 0; a < mam_coupling::num_aero_species(); ++a) {
      const char* cld_mmr_field_name = mam_coupling::cld_aero_mmr_field_name(m, a);
      if (strlen(cld_mmr_field_name) > 0) {
        wet_aero_.cld_aero_mmr[m][a] = get_field_out(cld_mmr_field_name).get_view<Real**>();
        dry_aero_.cld_aero_mmr[m][a] = buffer_.dry_cld_aero_mmr[m][a];
      }
    }
  }

  // set wet/dry aerosol-related gas state data
  for (int g = 0; g < mam_coupling::num_aero_gases(); ++g) {
    const char* mmr_field_name = mam_coupling::gas_mmr_field_name(g);
    wet_aero_.gas_mmr[g] = get_field_out(mmr_field_name).get_view<Real**>();
    dry_aero_.gas_mmr[g] = buffer_.dry_gas_mmr[g];
  }

  // FIXME: We need to get ssa_cmip6_sw_, af_cmip6_sw_, ext_cmip6_sw_, ext_cmip6_lw_ from a nc file.
  // aer_rad_props_sw inputs that are prescribed, i.e., we need a netcdf file.
  ssa_cmip6_sw_ = mam_coupling::view_3d ("ssa_cmip6_sw", ncol_, nlev_, nswbands_);
  af_cmip6_sw_  = mam_coupling::view_3d ("af_cmip6_sw", ncol_, nlev_, nswbands_);
  ext_cmip6_sw_ = mam_coupling::view_3d ("ext_cmip6_sw", ncol_, nswbands_, nlev_);
  ext_cmip6_lw_ = mam_coupling::view_3d("ext_cmip6_lw_", ncol_, nlev_, nlwbands_);

   // set up our preprocess/postprocess functors
  preprocess_.initialize(ncol_, nlev_, wet_atm_, wet_aero_, dry_atm_, dry_aero_);
  postprocess_.initialize(ncol_, nlev_, wet_atm_, wet_aero_, dry_atm_, dry_aero_);

  const int work_len = mam4::modal_aer_opt::get_work_len_aerosol_optics();
  work_ = mam_coupling::view_2d("work", ncol_, work_len);

  Kokkos::deep_copy(ssa_cmip6_sw_, 0.0);
  Kokkos::deep_copy(af_cmip6_sw_, 0.0);
  Kokkos::deep_copy(ext_cmip6_sw_, 0.0);
  Kokkos::deep_copy(ext_cmip6_lw_, 0.0);
 // read table info
{
  using namespace ShortFieldTagsNames;

  using view_1d_host = typename KT::view_1d<Real>::HostMirror;

    // Views in aerosol_optics_device_data_ are allocated in the following fuctions.
  // Note: these functions do not set values for aerosol_optics_device_data_.
  mam4::modal_aer_opt::set_complex_views_modal_aero(aerosol_optics_device_data_);
  mam4::modal_aer_opt::set_aerosol_optics_data_for_modal_aero_sw_views(aerosol_optics_device_data_);
  mam4::modal_aer_opt::set_aerosol_optics_data_for_modal_aero_lw_views(aerosol_optics_device_data_);

  mam_coupling::AerosolOpticsHostData aerosol_optics_host_data;

  std::map<std::string,FieldLayout>  layouts;
  std::map<std::string,view_1d_host> host_views;
  ekat::ParameterList rrtmg_params;

  mam_coupling::set_parameters_table(aerosol_optics_host_data,
                                     rrtmg_params,
                                     layouts,
                                     host_views );
  std::string mam_aerosol_optics_path="mam_aerosol_optics/";
  //FIXME: this name need to be pass in the input file.
  std::vector<std::string> name_table_modes=
  {
  mam_aerosol_optics_path+"mam4_mode1_rrtmg_aeronetdust_c141106.nc",
  mam_aerosol_optics_path+"mam4_mode2_rrtmg_c130628.nc",
  mam_aerosol_optics_path+"mam4_mode3_rrtmg_aeronetdust_c141106.nc",
  mam_aerosol_optics_path+"mam4_mode4_rrtmg_c130628.nc"
  };

  for (int imode = 0; imode < ntot_amode; imode++)
  {
    mam_coupling::read_rrtmg_table(name_table_modes[imode],
                                 imode,// mode No
                                 rrtmg_params, grid_,
                                 host_views,
                                 layouts,
                                 aerosol_optics_host_data,
                                 aerosol_optics_device_data_);
  }


// FIXME: we need to get this name from the yaml file.
std::string table_name_water = mam_aerosol_optics_path+"water_refindex_rrtmg_c080910.nc";
// it will syn data to device.
mam_coupling::read_water_refindex(table_name_water, grid_,
                                  aerosol_optics_device_data_.crefwlw,
                                  aerosol_optics_device_data_.crefwsw);
//
{
 // make a list of host views
  std::map<std::string,view_1d_host> host_views_aero;
  // defines layouts
  std::map<std::string,FieldLayout>  layouts_aero;
  ekat::ParameterList params_aero;
  std::string surname_aero="aer";
  mam_coupling::set_refindex(surname_aero, params_aero, host_views_aero, layouts_aero );

  // read data
  /*
  soa:s-organic: /compyfs/inputdata/atm/cam/physprops/ocphi_rrtmg_c100508.nc
  dst:dust:      /compyfs/inputdata/atm/cam/physprops/dust_aeronet_rrtmg_c141106.nc
  ncl:seasalt:   /compyfs/inputdata/atm/cam/physprops/ssam_rrtmg_c100508.nc
  so4:sulfate:   /compyfs/inputdata/atm/cam/physprops/sulfate_rrtmg_c080918.nc
  pom:p-organic: /compyfs/inputdata/atm/cam/physprops/ocpho_rrtmg_c130709.nc
  bc :black-c:   /compyfs/inputdata/atm/cam/physprops/bcpho_rrtmg_c100508.nc
  mom:m-organic: /compyfs/inputdata/atm/cam/physprops/poly_rrtmg_c130816.nc */
  std::vector<std::string> name_table_aerosols=
  {
  mam_aerosol_optics_path+"ocphi_rrtmg_c100508.nc", // soa:s-organic
  mam_aerosol_optics_path+"dust_aeronet_rrtmg_c141106.nc",//dst:dust:
  mam_aerosol_optics_path+"ssam_rrtmg_c100508.nc", // ncl:seasalt
  mam_aerosol_optics_path+"sulfate_rrtmg_c080918.nc", // so4:sulfate
  mam_aerosol_optics_path+"ocpho_rrtmg_c130709.nc", // pom:p-organic
  mam_aerosol_optics_path+"bcpho_rrtmg_c100508.nc", // bc :black-c
  mam_aerosol_optics_path+"poly_rrtmg_c130816.nc" // mom:m-organic
  };

  //FIXME: make a function that return a index given the species name
// specname_amode(ntot_aspectype) = (/ 'sulfate (0)   ',
//  'ammonium (1) ', 'nitrate (2)   ', &
      //  'p-organic (3) ', 's-organic (4) ', 'black-c (5)  ', &
      //  'seasalt (6)  ', 'dust  (7)    ', &
      //  'm-organic (8)' /)
  // FIXME: move this info to a conf file
  std::vector<int> species_ids=
  {
  4, // soa:s-organic
  7,//dst:dust:
  6, // ncl:seasalt
  0, // so4:sulfate
  3, // pom:p-organic
  5, // bc :black-c
  8 // mom:m-organic
  };


  constexpr int maxd_aspectype = mam4::ndrop::maxd_aspectype;
  auto specrefndxsw_host = mam_coupling::complex_view_2d::HostMirror(
        "specrefndxsw_host", nswbands_, maxd_aspectype);

  auto specrefndxlw_host = mam_coupling::complex_view_2d::HostMirror(
        "specrefndxlw_host", nlwbands_, maxd_aspectype);

  const int n_spec = size(name_table_aerosols);
  for (int ispec = 0; ispec < n_spec; ispec++)
  {
    // read data
    auto table_name = name_table_aerosols[ispec];
    // need to update table name
    params_aero.set("Filename",table_name);
    AtmosphereInput refindex_aerosol(params_aero, grid_,
    host_views_aero, layouts_aero);
    refindex_aerosol.read_variables();
    refindex_aerosol.finalize();

    // copy data to device
    int species_id =species_ids[ispec]; // FIXME: I need the species index for soa
    mam_coupling::set_refindex_aerosol(species_id,
                                      host_views_aero,
                                      specrefndxsw_host, // complex refractive index for water visible
                                      specrefndxlw_host);
  } // done ispec
  // reshape specrefndxsw_host and copy it to device
  mam4::modal_aer_opt::set_device_specrefindex(aerosol_optics_device_data_.specrefindex_sw, "short_wave", specrefndxsw_host);
  mam4::modal_aer_opt::set_device_specrefindex(aerosol_optics_device_data_.specrefindex_lw, "long_wave", specrefndxlw_host);
}

}

}
void MAMOptics::run_impl(const double dt) {

  constexpr Real zero =0.0;

  const auto policy = ekat::ExeSpaceUtils<KT::ExeSpace>::get_default_team_policy(ncol_, nlev_);
  const auto scan_policy = ekat::ExeSpaceUtils<KT::ExeSpace>::get_thread_range_parallel_scan_team_policy(ncol_, nlev_);
  // const auto policy = haero::ThreadTeamPolicy(1u, 1);

  // preprocess input -- needs a scan for the calculation of atm height
  Kokkos::parallel_for("preprocess", scan_policy, preprocess_);
  Kokkos::fence();
  /// outputs
  auto aero_g_sw   = get_field_out("aero_g_sw").get_view<Real***>();
  auto aero_ssa_sw = get_field_out("aero_ssa_sw").get_view<Real***>();
  auto aero_tau_sw = get_field_out("aero_tau_sw").get_view<Real***>();
  auto aero_tau_lw = get_field_out("aero_tau_lw").get_view<Real***>();

  auto aero_tau_forward = get_field_out("aero_tau_forward").get_view<Real***>();

  // NOTE: we do not compute this variable in aersol_optics
  auto aero_nccn   = get_field_out("nccn").get_view<Real**>(); // FIXME: get rid of this

  const Real t = 0.0;

  if (false) { // remove when ready to do actual calculations
    // populate these fields with reasonable representative values
    // Kokkos::deep_copy(aero_g_sw, 0.5);
    // Kokkos::deep_copy(aero_ssa_sw, 0.7);
    // Kokkos::deep_copy(aero_tau_sw, 0.0);
    // Kokkos::deep_copy(aero_tau_lw, 0.0);
    // Kokkos::deep_copy(aero_nccn, 50.0);
  } else {

    // Compute optical properties on all local columns.
    // (Strictly speaking, we don't need this parallel_for here yet, but we leave
    //  it in anticipation of column-specific aerosol optics to come.)
    Kokkos::parallel_for(policy, KOKKOS_LAMBDA(const ThreadTeam& team) {
      const Int icol = team.league_rank(); // column index
      auto odap_aer_icol = ekat::subview(aero_tau_lw, icol);

      // FIXME: Get rid of this
      // auto nccn = ekat::subview(aero_nccn, icol);
      auto pmid =  ekat::subview(dry_atm_.p_mid, icol);
      auto temperature =  ekat::subview(dry_atm_.T_mid, icol);
      auto cldn = ekat::subview(dry_atm_.cldfrac, icol);

      // FIXME: interface pressure [Pa]
      auto pint =  ekat::subview(p_int_, icol);
      auto zm =  ekat::subview(dry_atm_.z_mid, icol);
      // FIXME: dry mass pressure interval [Pa]
      // FIXME:
      auto zi= ekat::subview(dry_atm_.z_iface, icol);
      auto pdel = ekat::subview(p_del_, icol);
      auto pdeldry = ekat::subview(dry_atm_.p_del, icol);

      auto ssa_cmip6_sw_icol = ekat::subview(ssa_cmip6_sw_, icol);
      auto af_cmip6_sw_icol = ekat::subview(af_cmip6_sw_, icol);
      auto ext_cmip6_sw_icol = ekat::subview(ext_cmip6_sw_, icol);
      auto ext_cmip6_lw_icol = ekat::subview(ext_cmip6_lw_, icol);

      // FIXME: check if this correct: Note that these variables have pver+1 levels
      // tau_w =>  aero_ssa_sw  (pcols,0:pver,nswbands) ! aerosol single scattering albedo * tau
      auto tau_w_icol = ekat::subview(aero_ssa_sw, icol);
      // tau_w_g => "aero_g_sw" (pcols,0:pver,nswbands) ! aerosol assymetry parameter * tau * w
      auto tau_w_g_icol = ekat::subview(aero_g_sw, icol);
      // tau_w_f(pcols,0:pver,nswbands) => aero_tau_forward  ? ! aerosol forward scattered fraction * tau * w
      auto tau_w_f_icol = ekat::subview(aero_tau_forward, icol);
      // tau  => aero_tau_sw (?)   (pcols,0:pver,nswbands) ! aerosol extinction optical depth
      auto tau_icol = ekat::subview(aero_tau_sw, icol);

      auto work_icol = ekat::subview(work_, icol);

       // fetch column-specific subviews into aerosol prognostics
      // mam4::Prognostics progs = mam_coupling::interstitial_aerosols_for_column(dry_aero_, icol);
       mam4::Prognostics progs = mam_coupling::aerosols_for_column(dry_aero_, icol);

    mam4::aer_rad_props::aer_rad_props_sw(team,  dt, zi, pmid,
    pint, temperature,
    zm, progs,
    pdel, pdeldry,
    cldn, ssa_cmip6_sw_icol,
    af_cmip6_sw_icol, ext_cmip6_sw_icol,
    tau_icol, tau_w_icol, tau_w_g_icol,
    tau_w_f_icol,
    aerosol_optics_device_data_, work_icol);

team.team_barrier();

mam4::aer_rad_props::aer_rad_props_lw(team,
    dt, pmid, pint,
    temperature, zm, zi,
    progs, pdel, pdeldry,
    cldn, ext_cmip6_lw_icol,
    aerosol_optics_device_data_,
    odap_aer_icol);
    });

  }

  // postprocess output
  Kokkos::parallel_for("postprocess", policy, postprocess_);
  Kokkos::fence();
  printf("Done  with aerosol_optics \n");

  // Kokkos::deep_copy(aero_g_sw, 0.5);
  // Kokkos::deep_copy(aero_ssa_sw, 0.7);
  // Kokkos::deep_copy(aero_tau_sw, 0.0);
  // Kokkos::deep_copy(aero_tau_lw, 0.0);
  // Kokkos::deep_copy(aero_tau_forward, 0.0);
  // Kokkos::deep_copy(aero_nccn, 50.0);

}

void MAMOptics::finalize_impl()
{
}

} // namespace scream
