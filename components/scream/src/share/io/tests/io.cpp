#include <catch2/catch.hpp>

#include "scream_config.h"
#include "share/scream_types.hpp"

#include "share/io/output_manager.hpp"
#include "share/io/scorpio_output.hpp"
#include "share/io/scorpio_input.hpp"
#include "share/io/scream_scorpio_interface.hpp"

#include "share/grid/user_provided_grids_manager.hpp"
#include "share/grid/point_grid.hpp"

#include "share/field/field_identifier.hpp"
#include "share/field/field_header.hpp"
#include "share/field/field.hpp"
#include "share/field/field_repository.hpp"

#include "ekat/ekat_parameter_list.hpp"
namespace {
using namespace scream;
using namespace ekat::units;
using input_type = AtmosphereInput;

std::shared_ptr<FieldRepository<Real>> get_test_repo(const Int num_cols, const Int num_levs);
std::shared_ptr<UserProvidedGridsManager>            get_test_gm(const ekat::Comm io_comm, const Int num_cols, const Int num_levs);
ekat::ParameterList                                  get_om_params(const Int casenum);
ekat::ParameterList                                  get_in_params(const std::string type);

TEST_CASE("input_output_basic","io")
{

  ekat::Comm io_comm(MPI_COMM_WORLD);  // MPI communicator group used for I/O set as ekat object.
  Int num_cols = 2;
  Int num_levs = 3;

  // Initialize the pio_subsystem for this test:
  MPI_Fint fcomm = MPI_Comm_c2f(io_comm.mpi_comm());  // MPI communicator group used for I/O.  In our simple test we use MPI_COMM_WORLD, however a subset could be used.
  scorpio::eam_init_pio_subsystem(fcomm);   // Gather the initial PIO subsystem data creater by component coupler

  // First set up a field manager and grids manager to interact with the output functions
  std::shared_ptr<UserProvidedGridsManager> grid_man   = get_test_gm(io_comm,num_cols,num_levs);
  std::shared_ptr<FieldRepository<Real>>    field_repo = get_test_repo(num_cols,num_levs);

  // Create an Output manager for testing output
  OutputManager m_output_manager;
  auto output_params = get_om_params(1);
  m_output_manager.set_params(output_params);
  m_output_manager.set_comm(io_comm);
  m_output_manager.set_grids(grid_man);
  m_output_manager.set_repo(field_repo);
  m_output_manager.init();

  // Construct a timestamp
  util::TimeStamp time (0,0,0,0);

  //  Cycle through data and write output
  auto& out_fields = field_repo->get_field_groups().at("output");
  Int max_steps = 10;
  Real dt = 1.0;
  for (Int ii=0;ii<max_steps;++ii)
  {
    for (auto it : out_fields)
    {
      auto f_dev  = field_repo->get_field(it,"Physics").get_view();
      auto f_host = Kokkos::create_mirror_view( f_dev );
      Kokkos::deep_copy(f_host,f_dev);
      for (size_t jj=0;jj<f_host.size();++jj)
      {
        f_host(jj) += dt;
      }
      Kokkos::deep_copy(f_dev,f_host);
    }
    time += dt;
    m_output_manager.run(time);
  }
  m_output_manager.finalize();
  // At this point we should have 4 files output:
  // 1 file each for averaged, instantaneous, min and max data.
  // Cycle through each output and make sure it is correct.
  // We can use the produced output files to simultaneously check output quality and the
  // ability to read input.
  auto ins_params = get_in_params("Instant");
  auto avg_params = get_in_params("Average");
  auto min_params = get_in_params("Min");
  auto max_params = get_in_params("Max");
  Real tol = pow(10,-6);
  // Check instant output
  input_type ins_input(io_comm,ins_params,field_repo,grid_man);
  ins_input.pull_input();
  auto f1_dev = field_repo->get_field("field_1","Physics").get_view();
  auto f2_dev = field_repo->get_field("field_2","Physics").get_view();
  auto f3_dev = field_repo->get_field("field_3","Physics").get_reshaped_view<Real**>();
  auto f1_hst = Kokkos::create_mirror_view( f1_dev );
  auto f2_hst = Kokkos::create_mirror_view( f2_dev );
  auto f3_hst = Kokkos::create_mirror_view( f3_dev );
  Kokkos::deep_copy( f1_hst, f1_dev );
  Kokkos::deep_copy( f2_hst, f2_dev );
  Kokkos::deep_copy( f3_hst, f3_dev );
  for (int ii=0;ii<num_cols;++ii)
  {
    REQUIRE(std::abs(f1_hst(ii)-(max_steps*dt+ii))<tol);
    for (int jj=0;jj<num_levs;++jj)
    {
      REQUIRE(std::abs(f3_hst(ii,jj)-(ii+max_steps*dt + (jj+1)/10.))<tol);
    }
  }
  for (int jj=0;jj<num_levs;++jj)
  {
    REQUIRE(std::abs(f2_hst(jj)-(max_steps*dt + (jj+1)/10.))<tol);
  }
  // Check average output
  input_type avg_input(io_comm,avg_params,field_repo,grid_man);
  avg_input.pull_input();
  Kokkos::deep_copy( f1_hst, f1_dev );
  Kokkos::deep_copy( f2_hst, f2_dev );
  Kokkos::deep_copy( f3_hst, f3_dev );
  Real avg_val;
  for (int ii=0;ii<num_cols;++ii)
  {
    avg_val = (max_steps+1)/2.0*dt + ii; // Sum(x0+i*dt,i=1...N) = N*x0 + dt*N*(N+1)/2, AVG = Sum/N, note x0=ii in this case
    REQUIRE(std::abs(f1_hst(ii)-avg_val)<tol);
    for (int jj=0;jj<num_levs;++jj)
    {
      avg_val = (max_steps+1)/2.0*dt + (jj+1)/10.+ii;  //note x0=(jj+1)/10+ii in this case.
      REQUIRE(std::abs(f3_hst(ii,jj)-avg_val)<tol);
    }
  }
  for (int jj=0;jj<num_levs;++jj)
  {
    avg_val = (max_steps+1)/2.0*dt + (jj+1)/10.;  //note x0=(jj+1)/10 in this case.
    REQUIRE(std::abs(f2_hst(jj)-avg_val)<tol);
  }
  // Check max output
  // The max should be equivalent to the instantaneous because this function is monotonically increasing.
  input_type max_input(io_comm,max_params,field_repo,grid_man);
  max_input.pull_input();
  Kokkos::deep_copy( f1_hst, f1_dev );
  Kokkos::deep_copy( f2_hst, f2_dev );
  Kokkos::deep_copy( f3_hst, f3_dev );
  for (int ii=0;ii<num_cols;++ii)
  {
    REQUIRE(std::abs(f1_hst(ii)-(max_steps*dt+ii))<tol);
    for (int jj=0;jj<num_levs;++jj)
    {
      REQUIRE(std::abs(f3_hst(ii,jj)-(ii+max_steps*dt + (jj+1)/10.))<tol);
    }
  }
  for (int jj=0;jj<num_levs;++jj)
  {
    REQUIRE(std::abs(f2_hst(jj)-(max_steps*dt + (jj+1)/10.)<tol));
  }
  // Check min output
  // The min should be equivalent to the first step because this function is monotonically increasing.
  input_type min_input(io_comm,min_params,field_repo,grid_man);
  min_input.pull_input();
  Kokkos::deep_copy( f1_hst, f1_dev );
  Kokkos::deep_copy( f2_hst, f2_dev );
  Kokkos::deep_copy( f3_hst, f3_dev );
  for (int ii=0;ii<num_cols;++ii)
  {
    REQUIRE(std::abs(f1_hst(ii)-ii)<tol);
    for (int jj=0;jj<num_levs;++jj)
    {
      REQUIRE(std::abs(f3_hst(ii,jj)-(ii + (jj+1)/10.))<tol);
    }
  }
  for (int jj=0;jj<num_levs;++jj)
  {
    REQUIRE(std::abs(f2_hst(jj)-((jj+1)/10.))<tol);
  }
   
  scorpio::eam_pio_finalize();
  (*grid_man).clean_up();
} // TEST_CASE output_instance
/* ----------------------------------*/

/*===================================================================================================================*/
std::shared_ptr<FieldRepository<Real>> get_test_repo(const Int num_cols, const Int num_levs)
{
  // Create a repo
  std::shared_ptr<FieldRepository<Real>>  repo = std::make_shared<FieldRepository<Real>>();
  // Create some fields for this repo
  std::vector<FieldTag> tag_h  = {FieldTag::Column};
  std::vector<FieldTag> tag_v  = {FieldTag::VerticalLevel};
  std::vector<FieldTag> tag_2d = {FieldTag::Column,FieldTag::VerticalLevel};

  std::vector<Int>     dims_h  = {num_cols};
  std::vector<Int>     dims_v  = {num_levs};
  std::vector<Int>     dims_2d = {num_cols,num_levs};

  FieldIdentifier fid1("field_1",tag_h,m);
  FieldIdentifier fid2("field_2",tag_v,kg);
  FieldIdentifier fid3("field_3",tag_2d,kg/m);

  fid1.set_dimensions(dims_h);
  fid2.set_dimensions(dims_v);
  fid3.set_dimensions(dims_2d);

  fid1.set_grid_name("Physics");
  fid2.set_grid_name("Physics");
  fid3.set_grid_name("Physics");
  // Register fields with repo
  repo->registration_begins();
  repo->register_field(fid1,{"output"});
  repo->register_field(fid2,{"output","restart"});
  repo->register_field(fid3,{"output","restart"});
  repo->registration_ends();

  // Initialize these fields
  auto f1_dev = repo->get_field(fid1).get_view(); 
  auto f2_dev = repo->get_field(fid2).get_view(); 
  auto f3_dev = repo->get_field(fid3).get_reshaped_view<Real**>();
  auto f1_hst = Kokkos::create_mirror_view( f1_dev );
  auto f2_hst = Kokkos::create_mirror_view( f2_dev ); 
  auto f3_hst = Kokkos::create_mirror_view( f3_dev );
  Kokkos::deep_copy( f1_hst, f1_dev );
  Kokkos::deep_copy( f2_hst, f2_dev );
  Kokkos::deep_copy( f3_hst, f3_dev );
  for (int ii=0;ii<num_cols;++ii)
  {
    f1_hst(ii) = ii;
    for (int jj=0;jj<num_levs;++jj)
    {
      f2_hst(jj) = (jj+1)/10.0;
      f3_hst(ii,jj) = (ii) + (jj+1)/10.0;
    }
  }
  Kokkos::deep_copy(f1_dev,f1_hst);
  Kokkos::deep_copy(f2_dev,f2_hst);
  Kokkos::deep_copy(f3_dev,f3_hst);

  return repo;
}
/*===================================================================================================================*/
std::shared_ptr<UserProvidedGridsManager> get_test_gm(const ekat::Comm io_comm, const Int num_cols, const Int num_levs)
{

  auto& gm_factory = GridsManagerFactory::instance();
  gm_factory.register_product("User Provided",create_user_provided_grids_manager);
  auto dummy_grid = std::make_shared<PointGrid>(create_point_grid("Physics",num_cols,num_levs,io_comm));
  auto upgm = std::make_shared<UserProvidedGridsManager>();
  upgm->set_grid(dummy_grid);

  return upgm;
}
/*===================================================================================================================*/
ekat::ParameterList get_om_params(const Int casenum)
{
  ekat::ParameterList om_params("Output Manager");
  om_params.set<Int>("PIO Stride",1);
  if (casenum == 1)
  {
    om_params.set<std::vector<std::string>>("Output YAML Files",{"io_test_instant.yaml","io_test_average.yaml",
           "io_test_max.yaml","io_test_min.yaml"});
  } 
  else if (casenum == 2)
  {
    om_params.set<std::vector<std::string>>("Output YAML Files",{"io_test_restart.yaml"});
    auto& res_sub = om_params.sublist("Restart Control");
    auto& freq_sub = res_sub.sublist("FREQUENCY");
    freq_sub.set<Int>("OUT_N",5);
    freq_sub.set<std::string>("OUT_OPTION","Steps");
  }
  else
  {
    EKAT_REQUIRE_MSG(false,"Error, incorrect case number for get_om_params");
  }

  return om_params;  
}
/*===================================================================================================================*/
ekat::ParameterList get_in_params(const std::string type)
{
  ekat::ParameterList in_params("Input Parameters");
  in_params.set<std::string>("FILENAME","io_output_test."+type+".Steps_x10.0000-01-01.000010.nc");
  in_params.set<std::string>("GRID","Physics");
  auto& f_list = in_params.sublist("FIELDS");
  f_list.set<Int>("Number of Fields",3);
  for (int ii=1;ii<=3+1;++ii)
  {
    f_list.set<std::string>("field "+std::to_string(ii),"field_"+std::to_string(ii));
  }
  return in_params;
}
/*===================================================================================================================*/
} // undefined namespace
