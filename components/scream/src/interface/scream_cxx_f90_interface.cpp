#include "share/scream_session.hpp"

#include "share/atmosphere_process.hpp"
#include "share/scream_pack.hpp"
#include "share/grid/user_provided_grids_manager.hpp"
#include "share/grid/se_grid.hpp"
#include "control/atmosphere_driver.hpp"

#include "physics/p3/atmosphere_microphysics.hpp"
#include "physics/p3/scream_p3_interface.hpp"
#include "physics/p3/p3_functions_f90.hpp"
#include "physics/shoc/atmosphere_macrophysics.hpp"
#include "physics/shoc/scream_shoc_interface.hpp"

#include "control/tests/dummy_grid.hpp"

#include "interface/ScreamContext.hpp"
#include "share/mpi/scream_comm.hpp"

extern "C"
{

/*===============================================================================================*/
void scream_init (const MPI_Fint& f_comm, const int& start_ymd, const int& start_tod) {
  using namespace scream;
  using namespace scream::control;

  // First of all, disable all fpes we may have enabled.
  // Store the mask, so we can restore before returning.
  int fpe_mask = get_enabled_fpes();
  disable_all_fpes();
  constexpr int num_cols  = 32;

  // First of all, initialize the scream session
  initialize_scream_session();
  // Get the context
  auto& c = ScreamContext::singleton();
  // Create the C MPI_Comm from the Fortran one
  MPI_Comm mpi_comm_c = MPI_Comm_f2c(f_comm);
  auto& comm = c.create<Comm>(mpi_comm_c);

  // Create a parameter list for inputs
  ParameterList ad_params("Atmosphere Driver");
  auto& proc_params = ad_params.sublist("Atmosphere Processes");

  proc_params.set("Number of Entries",3);
  proc_params.set<std::string>("Schedule Type","Sequential");

  auto& p0 = proc_params.sublist("Process 0");
  p0.set<std::string>("Process Name", "SA");
  p0.set<std::string>("Grid","Physics");
  auto& p1 = proc_params.sublist("Process 1");
  p1.set<std::string>("Process Name", "P3");
  p1.set<std::string>("Grid","Physics");
  auto& p2 = proc_params.sublist("Process 2");
  p2.set<std::string>("Process Name", "SHOC");
  p2.set<std::string>("Grid","Physics");

  auto& gm_params = ad_params.sublist("Grids Manager");
  gm_params.set<std::string>("Type","User Provided");
  gm_params.set<std::string>("Reference Grid","Physics");

  // Need to register products in the factory *before* we create any AtmosphereProcessGroup,
  // which rely on factory for process creation. The initialize method of the AD does that.
  // While we're at it, check that the case insensitive key of the factory works.
  auto& proc_factory = AtmosphereProcessFactory::instance();
  proc_factory.register_product("SA",&create_atmosphere_process<P3StandAloneInit>);
  proc_factory.register_product("p3",&create_atmosphere_process<P3Microphysics>);
  proc_factory.register_product("SHOC",&create_atmosphere_process<SHOCMacrophysics>);

  // Need to register grids managers before we create the driver
  auto& gm_factory = GridsManagerFactory::instance();
  gm_factory.register_product("User Provided",create_user_provided_grids_manager);

  // Set the dummy grid in the UserProvidedGridManager
  // Recall that this class stores *static* members, so whatever
  // we set here, will be reflected in the GM built by the factory.
  auto& upgm = c.create<UserProvidedGridsManager>(); // upgm;
  upgm.set_grid(std::make_shared<DummyPhysicsGrid>(num_cols));
  upgm.set_reference_grid("Physics");

  // Create a comm
  Comm atm_comm (MPI_COMM_WORLD);

  // Create the bare ad, then init it
  // TODO: uncomment once you have valid inputs. I fear AD may crash with no inputs.
  auto& ad = c.create<AtmosphereDriver>();

  // Recall that e3sm uses the int YYYYMMDD to store a date
  std::cout << "start_ymd: " << start_ymd << "\n";
  const int dd = start_ymd % 100;
  const int mm = (start_ymd / 100) % 100;
  const int yy = start_ymd / 10000;
  util::TimeStamp time (yy,mm,dd,start_tod);

  // Init and run (to finalize, wait till checks are completed,
  // or you'll clear the field repo!)
  ad.initialize(atm_comm,ad_params,time);

  (void) start_ymd;
  (void) start_tod;
  (void) f_comm;

  // Restore the FPE flag as it was when control was handed to us.
  disable_all_fpes();
  enable_fpes(fpe_mask);
}
/*===============================================================================================*/
void scream_run (const double& dt) {
  // TODO: uncomment once you have valid inputs. I fear AD may crash with no inputs.
  using namespace scream;
  using namespace scream::control;

  // First of all, enable only scream fpes.
  // Store the mask, so we can restore before returning.
  int fpe_mask = get_enabled_fpes();
  disable_all_fpes();
  enable_default_fpes();

  // Get the context
  auto& c = ScreamContext::singleton();

  // Get the AD, and run it
  auto& ad = c.getNonConst<AtmosphereDriver>();
  ad.run(dt);

  (void) dt;

  // Restore the FPE flag as it was when control was handed to us.
  disable_all_fpes();
  enable_fpes(fpe_mask);
}

/*===============================================================================================*/
void scream_finalize (/* args ? */) {
  using namespace scream;
  using namespace scream::control;

  // First of all, enable only scream fpes.
  // Store the mask, so we can restore before returning.
  int fpe_mask = get_enabled_fpes();
  disable_all_fpes();
  enable_default_fpes();

  // TODO: uncomment once you have valid inputs. I fear AD may crash with no inputs.
  // Get the context
  auto& c = ScreamContext::singleton();

  // Get the AD, and finalize it
  auto& ad = c.getNonConst<AtmosphereDriver>();
  auto& upgm = c.getNonConst<UserProvidedGridsManager>();
  ad.finalize();
  upgm.clean_up();
  p3::P3GlobalForFortran::deinit();

  // Restore the FPE flag as it was when control was handed to us.
  disable_all_fpes();
  enable_fpes(fpe_mask);
}

} // extern "C"
