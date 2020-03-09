#include "catch2/catch.hpp"

#include "share/scream_types.hpp"
#include "share/util/scream_utils.hpp"
#include "share/scream_kokkos.hpp"
#include "share/scream_pack.hpp"
#include "physics/p3/p3_functions.hpp"
#include "physics/p3/p3_functions_f90.hpp"
#include "share/util/scream_kokkos_utils.hpp"
#include "share/util/scream_arch.hpp"

#include "p3_unit_tests_common.hpp"

#include <thread>
#include <array>
#include <algorithm>
#include <random>

namespace scream {
namespace p3 {
namespace unit_test {
/*
 * Unit-tests for p3_functions.
 */
template <typename D>
struct UnitWrap::UnitTest<D>::TestP3Func
{
  KOKKOS_FUNCTION  static void saturation_tests(const Scalar& temperature, const Scalar& pressure, const Scalar& correct_sat_ice_p,
    const Scalar& correct_sat_liq_p, const Scalar&  correct_mix_ice_r, const Scalar& correct_mix_liq_r, int& errors ){

    const Spack temps(temperature);
    const Spack pres(pressure);

    Spack sat_ice_p = Functions::polysvp1(temps, true);
    Spack sat_liq_p = Functions::polysvp1(temps, false);

    Spack mix_ice_r = Functions::qv_sat(temps, pres, true);
    Spack mix_liq_r = Functions::qv_sat(temps, pres, false);

    // The correct results were computed with double precision, so we need
    // significantly greater tolerance for single precision.
    Scalar tol = (util::is_single_precision<Scalar>::value || util::OnGpu<ExeSpace>::value) ? C::Tol*100 : C::Tol;

    for(int s = 0; s < sat_ice_p.n; ++s){
      // Test vapor pressure
      if (abs(sat_ice_p[s] - correct_sat_ice_p) > tol ) {errors++;}
      if (abs(sat_liq_p[s] - correct_sat_liq_p) > tol)  {errors++;}
      //Test mixing-ratios
      if (abs(mix_ice_r[s] -  correct_mix_ice_r) > tol ) {errors++;}
      if (abs(mix_liq_r[s] -  correct_mix_liq_r) > tol ) {errors++;}
    }
}

  static void run()
  {
    int nerr = 0;
    TeamPolicy policy(util::ExeSpaceUtils<ExeSpace>::get_default_team_policy(1, 1));
    Kokkos::parallel_reduce("TestTableIce::run", policy, KOKKOS_LAMBDA(const MemberType& team, int& errors) {

      errors = 0;
      const auto tmelt = C::Tmelt;
      // Test values @ the melting point of H20 @ 1e5 Pa
      saturation_tests(tmelt, 1e5, 610.7960763188032, 610.7960763188032,
        0.003822318507864685,  0.003822318507864685, errors);

      //Test vaules @ 243.15K @ 1e5 Pa
      saturation_tests(243.15, 1e5, 37.98530141245404, 50.98455924912173,
         0.00023634717905493638,  0.0003172707211143376, errors);

      //Test values @ 303.15 @ 1e5 Pa
      saturation_tests(303.15, 1e5, 4242.757341329608, 4242.757341329608,
        0.0275579183092878, 0.0275579183092878, errors);

    }, nerr);

    Kokkos::fence();
    //REQUIRE(nerr == 0);
  }
};

template <typename D>
struct UnitWrap::UnitTest<D>::TestP3Conservation
{

static void cloud_water_conservation_tests_device(){

    using KTH = KokkosTypes<HostDevice>;

    CloudWaterConservationData cwdc[1] = {{sp(1e-5), 0.0, sp(1.1), sp(1e-4), 0.0, 0.0, 0.0, 0.0, 0.0, sp(1.0), sp(1.0)}};

    // Sync to device
    KTH::view_1d<CloudWaterConservationData> cwdc_host("cwdc_host", 1);
    view_1d<CloudWaterConservationData> cwdc_device("cwdc_host", 1);

    // This copy only copies the input variables.
    std::copy(&cwdc[0], &cwdc[0] + 1, cwdc_host.data());
    Kokkos::deep_copy(cwdc_device, cwdc_host);

    // Run the lookup from a kernel and copy results back to host
    Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
      Spack qc(cwdc_device(0).qc);
      Spack qcnuc(cwdc_device(0).qcnuc);
      Spack qcaut(cwdc_device(0).qcaut);
      Spack qcacc(cwdc_device(0).qcacc);
      Spack qccol(cwdc_device(0).qccol);
      Spack qcheti(cwdc_device(0).qcheti);
      Spack qcshd(cwdc_device(0).qcshd);
      Spack qiberg(cwdc_device(0).qiberg);
      Spack qisub(cwdc_device(0).qisub);
      Spack qidep(cwdc_device(0).qidep);

      Functions::cloud_water_conservation(qc, qcnuc, cwdc_device(0).dt, qcaut, qcacc, qccol, qcheti, qcshd, qiberg, qisub, qidep);

      cwdc_device(0).qc = qc[0];
      cwdc_device(0).qcnuc = qcnuc[0];
      cwdc_device(0).qcaut = qcaut[0];
      cwdc_device(0).qcacc = qcacc[0];
      cwdc_device(0).qccol = qccol[0];
      cwdc_device(0).qcheti = qcheti[0];
      cwdc_device(0).qcshd = qcshd[0];
      cwdc_device(0).qiberg = qiberg[0];
      cwdc_device(0).qisub = qisub[0];
      cwdc_device(0).qidep = qidep[0];
    });

    // Sync back to host
    Kokkos::deep_copy(cwdc_host, cwdc_device);

    const auto ratio = cwdc[0].qc/(cwdc[0].qcaut * cwdc[0].dt);
    REQUIRE(abs(cwdc_host(0).qcaut - cwdc[0].qcaut*ratio) <= C::Tol);
    REQUIRE(cwdc_host(0).qcacc == 0.0);
    REQUIRE(cwdc_host(0).qccol == 0.0);
    REQUIRE(cwdc_host(0).qcheti == 0.0);
    REQUIRE(cwdc_host(0).qcshd == 0.0);
    REQUIRE(cwdc_host(0).qiberg == 0.0);
    REQUIRE(abs(cwdc_host(0).qisub -(1.0 - ratio)) <= C::Tol);
    REQUIRE(abs(cwdc_host(0).qidep - (1.0 - ratio)) <= C::Tol);
    REQUIRE(cwdc_host[0].qcaut * cwdc[0].dt <= cwdc_host[0].qc);
  }

  static void rain_water_conservation_tests_device(){
     using KTH = KokkosTypes<HostDevice>;

     RainWaterConservationData rwdc[1] = {{sp(1e-5), 0.0, 0.0, 0.0, 0.0, sp(1.1), sp(1e-4), 0.0, 0.0 }};

     // Sync to device
     KTH::view_1d<RainWaterConservationData> rwdc_host("rwdc_host", 1);
     view_1d<RainWaterConservationData> rwdc_device("rwdc_host", 1);

     // This copy only copies the input variables.
     std::copy(&rwdc[0], &rwdc[0] + 1, rwdc_host.data());
     Kokkos::deep_copy(rwdc_device, rwdc_host);

    // Run the lookup from a kernel and copy results back to host
    Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
      Spack qr(rwdc_device(0).qr);
      Spack qcaut(rwdc_device(0).qcaut);
      Spack qcacc(rwdc_device(0).qcacc);
      Spack qimlt(rwdc_device(0).qimlt);
      Spack qcshd(rwdc_device(0).qcshd);
      Spack qrevp(rwdc_device(0).qrevp);
      Spack qrcol(rwdc_device(0).qrcol);
      Spack qrheti(rwdc_device(0).qrheti);

      Functions::rain_water_conservation(qr, qcaut, qcacc, qimlt, qcshd, rwdc_device(0).dt, qrevp, qrcol, qrheti);

      rwdc_device(0).qr = qr[0];
      rwdc_device(0).qcaut = qcaut[0];
      rwdc_device(0).qcacc = qcacc[0];
      rwdc_device(0).qimlt = qimlt[0];
      rwdc_device(0).qcshd = qcshd[0];
      rwdc_device(0).qrevp = qrevp[0];
      rwdc_device(0).qrcol = qrcol[0];
      rwdc_device(0).qrheti = qrheti[0];
    });


    // Sync back to host
    Kokkos::deep_copy(rwdc_host, rwdc_device);
    const auto ratio = rwdc[0].qr/(rwdc[0].qrevp * rwdc[0].dt);

    //Here we check cases where source > sinks and sinks > 1e-20
    REQUIRE(rwdc_host(0).qcaut == 0.0);
    REQUIRE(rwdc_host(0).qcacc == 0.0);
    REQUIRE(rwdc_host(0).qimlt == 0.0);
    REQUIRE(rwdc_host(0).qcshd == 0.0);

    //Check the value of qrevp
    REQUIRE(abs(rwdc_host(0).qrevp- rwdc[0].qrevp*ratio)<= C::Tol);

    //Now test that conservation has actually been enforced
    REQUIRE( rwdc_host(0).qrevp * rwdc_host(0).dt  <= rwdc_host(0).qr);

  }


  static void ice_water_conservation_tests_device(){
    using KTH = KokkosTypes<HostDevice>;

    IceWaterConservationData iwdc[1] = {{sp(1e-5), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, sp(1.1), sp(1e-4), 0.0}};

    // Sync to device
    KTH::view_1d<IceWaterConservationData> iwdc_host("iwdc_host", 1);
    view_1d<IceWaterConservationData> iwdc_device("iwdc_host", 1);

    // This copy only copies the input variables.
    std::copy(&iwdc[0], &iwdc[0] + 1, iwdc_host.data());
    Kokkos::deep_copy(iwdc_device, iwdc_host);

    // Run the lookup from a kernel and copy results back to host
    Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
      Spack qitot(iwdc_device(0).qitot);
      Spack qidep(iwdc_device(0).qidep);
      Spack qinuc(iwdc_device(0).qinuc);
      Spack qrcol(iwdc_device(0).qrcol);
      Spack qccol(iwdc_device(0).qccol);
      Spack qrheti(iwdc_device(0).qrheti);
      Spack qcheti(iwdc_device(0).qcheti);
      Spack qiberg(iwdc_device(0).qiberg);
      Spack qisub(iwdc_device(0).qisub);
      Spack qimlt(iwdc_device(0).qimlt);

      Functions::ice_water_conservation(qitot, qidep, qinuc, qrcol, qccol, qrheti, qcheti, qiberg, iwdc_device(0).dt, qisub, qimlt);

      iwdc_device(0).qitot = qitot[0];
      iwdc_device(0).qidep = qidep[0];
      iwdc_device(0).qinuc = qinuc[0];
      iwdc_device(0).qrcol = qrcol[0];
      iwdc_device(0).qccol = qccol[0];
      iwdc_device(0).qrheti = qrheti[0];
      iwdc_device(0).qcheti = qcheti[0];
      iwdc_device(0).qiberg = qiberg[0];
      iwdc_device(0).qisub = qisub[0];
      iwdc_device(0).qimlt = qimlt[0];
    });

  }

  static void run()
  {

    cloud_water_conservation_tests_device();

    rain_water_conservation_tests_device();

    ice_water_conservation_tests_device();

  }

  static void cloud_water_conservation_unit_bfb_tests(){

    using KTH = KokkosTypes<HostDevice>;

    static constexpr Int max_pack_size = 16;
    REQUIRE(Spack::n <= max_pack_size);

    CloudWaterConservationData cwdc[max_pack_size] = {
      //qc, qcnuc, cwdc_device(0).dt, qcaut, qcacc, qccol, qcheti, qcshd, qiberg, qisub, qidep
      {9.9999999999999995e-7, 0.0, 1800.0, 1.5832574016248739e-12, 1.0630996907148179e-12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {6.4285714285714288e-5, 0.0, 1800.0, 5.0577951315583066e-7, 7.7585489624948031e-4, 1.5683327213659326E-4, 1.2893174331809564e-14, 0.0, 5.0463073442953805e-6, 0.0, 5.1387602886199180e-7},
      {0.0, 0.0, 1800.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {7.1428571428571434e-5, 0.0, 1800.0, 5.1480988828550771e-7, 7.7585489624948031e-4, 1.5597668529004373e-4, 4.9926620576534573e-14, 0.0, 6.7718890050008472e-6, 0.0, 7.1052455549903861e-7},

      {9.9999999999999995e-7, 0.0, 1800.0, 1.5832574016248739e-12, 1.0630996907148179e-12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {6.4285714285714288e-5, 0.0, 1800.0, 5.0577951315583066e-7, 7.7585489624948031e-4, 1.5683327213659326E-4, 1.2893174331809564e-14, 0.0, 5.0463073442953805e-6, 0.0, 5.1387602886199180e-7},
      {0.0, 0.0, 1800.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {7.1428571428571434e-5, 0.0, 1800.0, 5.1480988828550771e-7, 7.7585489624948031e-4, 1.5597668529004373e-4, 4.9926620576534573e-14, 0.0, 6.7718890050008472e-6, 0.0, 7.1052455549903861e-7},

      {9.9999999999999995e-7, 0.0, 1800.0, 1.5832574016248739e-12, 1.0630996907148179e-12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {6.4285714285714288e-5, 0.0, 1800.0, 5.0577951315583066e-7, 7.7585489624948031e-4, 1.5683327213659326E-4, 1.2893174331809564e-14, 0.0, 5.0463073442953805e-6, 0.0, 5.1387602886199180e-7},
      {0.0, 0.0, 1800.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {7.1428571428571434e-5, 0.0, 1800.0, 5.1480988828550771e-7, 7.7585489624948031e-4, 1.5597668529004373e-4, 4.9926620576534573e-14, 0.0, 6.7718890050008472e-6, 0.0, 7.1052455549903861e-7},

      {9.9999999999999995e-7, 0.0, 1800.0, 1.5832574016248739e-12, 1.0630996907148179e-12, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {6.4285714285714288e-5, 0.0, 1800.0, 5.0577951315583066e-7, 7.7585489624948031e-4, 1.5683327213659326E-4, 1.2893174331809564e-14, 0.0, 5.0463073442953805e-6, 0.0, 5.1387602886199180e-7},
      {0.0, 0.0, 1800.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {7.1428571428571434e-5, 0.0, 1800.0, 5.1480988828550771e-7, 7.7585489624948031e-4, 1.5597668529004373e-4, 4.9926620576534573e-14, 0.0, 6.7718890050008472e-6, 0.0, 7.1052455549903861e-7}
    };

    // Sync to device
    KTH::view_1d<CloudWaterConservationData> cwdc_host("cwdc_host", Spack::n);
    view_1d<CloudWaterConservationData> cwdc_device("cwdc_host", Spack::n);

    // This copy only copies the input variables.
    std::copy(&cwdc[0], &cwdc[0] + Spack::n, cwdc_host.data());
    Kokkos::deep_copy(cwdc_device, cwdc_host);

    // Get data from fortran
    for (Int i = 0; i < max_pack_size; ++i) {
      cloud_water_conservation(cwdc[i]);
    }

    // This copy also copies the output from the fortran function into the host view. These values
    // are need to check the values returned from
    std::copy(&cwdc[0], &cwdc[0] + Spack::n, cwdc_host.data());

    // Run the lookup from a kernel and copy results back to host
    Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
      // Init pack inputs
      Spack qc, qcnuc, qcaut, qcacc, qccol, qcheti, qcshd, qiberg, qisub, qidep;
      for (Int s = 0; s < Spack::n; ++s) {
        qc[s] = cwdc_device(s).qc;
        qcnuc[s] = cwdc_device(s).qcnuc;
        qcaut[s] = cwdc_device(s).qcaut;
        qcacc[s] = cwdc_device(s).qcacc;
        qccol[s] = cwdc_device(s).qccol;
        qcheti[s] = cwdc_device(s).qcheti;
        qcshd[s] = cwdc_device(s).qcshd;
        qiberg[s] = cwdc_device(s).qiberg;
        qisub[s] = cwdc_device(s).qisub;
        qidep[s] = cwdc_device(s).qidep;
      }

      Functions::cloud_water_conservation(qc, qcnuc, cwdc_device(0).dt, qcaut, qcacc, qccol, qcheti, qcshd, qiberg, qisub, qidep);
      // Copy results back into views
      for (Int s = 0; s < Spack::n; ++s) {
        cwdc_device(s).qc = qc[s];
        cwdc_device(s).qcnuc = qcnuc[s];
        cwdc_device(s).qcaut = qcaut[s];
        cwdc_device(s).qcacc = qcacc[s];
        cwdc_device(s).qccol = qccol[s];
        cwdc_device(s).qcheti = qcheti[s];
        cwdc_device(s).qiberg = qiberg[s];
        cwdc_device(s).qisub = qisub[s];
        cwdc_device(s).qidep = qidep[s];
      }

    });
    // Sync back to host
    Kokkos::deep_copy(cwdc_host, cwdc_device);

    // Validate results
    for (Int s = 0; s < Spack::n; ++s) {
      REQUIRE(cwdc[s].qc == cwdc_host(s).qc);
      REQUIRE(cwdc[s].qcnuc == cwdc_host(s).qcnuc);
      REQUIRE(cwdc[s].qcaut == cwdc_host(s).qcaut);
      REQUIRE(cwdc[s].qcacc == cwdc_host(s).qcacc);
      REQUIRE(cwdc[s].qccol == cwdc_host(s).qccol);
      REQUIRE(cwdc[s].qcheti == cwdc_host(s).qcheti);
      REQUIRE(cwdc[s].qiberg == cwdc_host(s).qiberg);
      REQUIRE(cwdc[s].qisub == cwdc_host(s).qisub);
      REQUIRE(cwdc[s].qidep == cwdc_host(s).qidep);
    }

  }

  static void ice_water_conservation_unit_bfb_tests()
  {
    using KTH = KokkosTypes<HostDevice>;

    static constexpr Int max_pack_size = 16;
    REQUIRE(Spack::n <= max_pack_size);

    IceWaterConservationData iwdc[max_pack_size] = {
      // qitot, qidep, qinuc, qiberg, qrcol, qccol, qrheti, qcheti, iwdc_device(0).dt, qisub, qimlt
      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 1.9205467584100191e-4},
      {5.0e-8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 1.8234653652173277e-7, 0.0},
      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 2.3237448636383435e-3},
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0},

      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 1.9205467584100191e-4},
      {5.0e-8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 1.8234653652173277e-7, 0.0},
      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 2.3237448636383435e-3},
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0},

      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 1.9205467584100191e-4},
      {5.0e-8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 1.8234653652173277e-7, 0.0},
      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 2.3237448636383435e-3},
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0},

      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 1.9205467584100191e-4},
      {5.0e-8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 1.8234653652173277e-7, 0.0},
      {1.0e-4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 2.3237448636383435e-3},
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0}
    };

    // Sync to device
    KTH::view_1d<IceWaterConservationData> iwdc_host("iwdc_host", Spack::n);
    view_1d<IceWaterConservationData> iwdc_device("iwdc_host", Spack::n);

    // This copy only copies the input variables.
    std::copy(&iwdc[0], &iwdc[0] + Spack::n, iwdc_host.data());
    Kokkos::deep_copy(iwdc_device, iwdc_host);

    // Get data from fortran
    for (Int i = 0; i < max_pack_size; ++i) {
      ice_water_conservation(iwdc[i]);
    }

    // This copy also copies the output from the fortran function into the host view. These values
    // are need to check the values returned from
    std::copy(&iwdc[0], &iwdc[0] + Spack::n, iwdc_host.data());

    // Run the lookup from a kernel and copy results back to host
    Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
      // Init pack inputs
      Spack qitot,qidep,qinuc,qiberg,qrcol,qccol,qrheti,qcheti,qisub,qimlt;
      for (Int s = 0; s < Spack::n; ++s) {
        qitot[s]  = iwdc_device(s).qitot;
        qidep[s]  = iwdc_device(s).qidep;
        qinuc[s]  = iwdc_device(s).qinuc;
        qiberg[s] = iwdc_device(s).qiberg;
        qrcol[s]  = iwdc_device(s).qrcol;
        qccol[s]  = iwdc_device(s).qccol;
        qrheti[s] = iwdc_device(s).qrheti;
        qcheti[s] = iwdc_device(s).qcheti;
        qisub[s] = iwdc_device(s).qisub;
        qimlt[s] = iwdc_device(s).qimlt;
      }

      Functions::ice_water_conservation(qitot, qidep, qinuc, qiberg, qrcol, qccol, qrheti, qcheti, iwdc_device(0).dt, qisub, qimlt);
      // Copy results back into views
      for (Int s = 0; s < Spack::n; ++s) {
        iwdc_device(s).qitot = qitot[s];
        iwdc_device(s).qidep = qidep[s];
        iwdc_device(s).qinuc = qinuc[s];
        iwdc_device(s).qiberg = qiberg[s];
        iwdc_device(s).qrcol = qrcol[s];
        iwdc_device(s).qccol = qccol[s];
        iwdc_device(s).qrheti = qrheti[s];
        iwdc_device(s).qcheti = qcheti[s];
        iwdc_device(s).qisub = qisub[s];
        iwdc_device(s).qimlt = qimlt[s];
      }

    });

    // Sync back to host
    Kokkos::deep_copy(iwdc_host, iwdc_device);

    // Validate results
    for (Int s = 0; s < Spack::n; ++s) {
      REQUIRE(iwdc[s].qitot == iwdc_host(s).qitot);
      REQUIRE(iwdc[s].qidep == iwdc_host(s).qidep );
      REQUIRE(iwdc[s].qinuc == iwdc_host(s).qinuc);
      REQUIRE(iwdc[s].qiberg == iwdc_host(s).qiberg);
      REQUIRE(iwdc[s].qrcol  == iwdc_host(s).qrcol);
      REQUIRE(iwdc[s].qccol == iwdc_host(s).qccol);
      REQUIRE(iwdc[s].qrheti == iwdc_host(s).qrheti);
      REQUIRE(iwdc[s].qcheti == iwdc_host(s).qcheti);
      REQUIRE(iwdc[s].qisub == iwdc_host(s).qisub);
      REQUIRE(iwdc[s].qimlt == iwdc_host(s).qimlt);
    }

  }

  static void rain_water_conservation_unit_bfb_tests(){

    using KTH = KokkosTypes<HostDevice>;

    static constexpr Int max_pack_size = 16;
    REQUIRE(Spack::n <= max_pack_size);

    RainWaterConservationData rwdc[max_pack_size] = {
      // qr, qcaut, qcacc, qimlt, qcshd, rwdc_device(0).dt, qrevp, qrcol, qrheti
      {0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0, 0.0},
      {3.6842105263157901e-6, 1.8910609577335389e-12, 6.5659507736611415e-9, 2.0267066625093075e-3, 1.3686661018890648e-9, 1800.0, 0.0, 0.0, 0.0},
      {1.0000000000000001e-5, 1.3239078166546396e-11, 4.5967389456540289e-8, 0.0, 0.0, 1800.0, 0.0, 1.4619847302347994e-33, 1.3104200383028957e-8},
      {8.9473684210526319e-6, 1.1338778389922441e-11, 3.9369360589471763e-8, 0.0, 0.0, 1800.0, 0.0, 1.4495908589465900e-33, 8.5051489557327688e-10},

      {0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0, 0.0},
      {3.6842105263157901e-6, 1.8910609577335389e-12, 6.5659507736611415e-9, 2.0267066625093075e-3, 1.3686661018890648e-9, 1800.0, 0.0, 0.0, 0.0},
      {1.0000000000000001e-5, 1.3239078166546396e-11, 4.5967389456540289e-8, 0.0, 0.0, 1800.0, 0.0, 1.4619847302347994e-33, 1.3104200383028957e-8},
      {8.9473684210526319e-6, 1.1338778389922441e-11, 3.9369360589471763e-8, 0.0, 0.0, 1800.0, 0.0, 1.4495908589465900e-33, 8.5051489557327688e-10},

      {0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0, 0.0},
      {3.6842105263157901e-6, 1.8910609577335389e-12, 6.5659507736611415e-9, 2.0267066625093075e-3, 1.3686661018890648e-9, 1800.0, 0.0, 0.0, 0.0},
      {1.0000000000000001e-5, 1.3239078166546396e-11, 4.5967389456540289e-8, 0.0, 0.0, 1800.0, 0.0, 1.4619847302347994e-33, 1.3104200383028957e-8},
      {8.9473684210526319e-6, 1.1338778389922441e-11, 3.9369360589471763e-8, 0.0, 0.0, 1800.0, 0.0, 1.4495908589465900e-33, 8.5051489557327688e-10},

      {0.0, 0.0, 0.0, 0.0, 0.0, 1800.0, 0.0, 0.0, 0.0},
      {3.6842105263157901e-6, 1.8910609577335389e-12, 6.5659507736611415e-9, 2.0267066625093075e-3, 1.3686661018890648e-9, 1800.0, 0.0, 0.0, 0.0},
      {1.0000000000000001e-5, 1.3239078166546396e-11, 4.5967389456540289e-8, 0.0, 0.0, 1800.0, 0.0, 1.4619847302347994e-33, 1.3104200383028957e-8},
      {8.9473684210526319e-6, 1.1338778389922441e-11, 3.9369360589471763e-8, 0.0, 0.0, 1800.0, 0.0, 1.4495908589465900e-33, 8.5051489557327688e-10}
    };

    // Sync to device
    KTH::view_1d<RainWaterConservationData> rwdc_host("rwdc_host", Spack::n);
    view_1d<RainWaterConservationData> rwdc_device("rwdc_host", Spack::n);

    // This copy only copies the input variables.
    std::copy(&rwdc[0], &rwdc[0] + Spack::n, rwdc_host.data());
    Kokkos::deep_copy(rwdc_device, rwdc_host);

    // Get data from fortran
    for (Int i = 0; i < max_pack_size; ++i) {
      rain_water_conservation(rwdc[i]);
    }

    // This copy also copies the output from the fortran function into the host view. These values
    // are need to check the values returned from
    std::copy(&rwdc[0], &rwdc[0] + Spack::n, rwdc_host.data());

    // Run the lookup from a kernel and copy results back to host
    Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
      // Init pack inputs
      Spack qr, qcaut, qcacc, qimlt, qcshd, qrevp, qrcol, qrheti;
      for (Int s = 0; s < Spack::n; ++s) {
        qr[s] = rwdc_device(s).qr;
        qcaut[s] = rwdc_device(s).qcaut;
        qcacc[s] = rwdc_device(s).qcacc;
        qimlt[s] = rwdc_device(s).qimlt;
        qcshd[s] = rwdc_device(s).qcshd;
        qrevp[s] = rwdc_device(s).qrevp;
        qrcol[s] = rwdc_device(s).qrcol;
        qrheti[s] = rwdc_device(s).qrheti;
      }

      Functions::rain_water_conservation(qr, qcaut, qcacc, qimlt, qcshd, rwdc_device(0).dt, qrevp, qrcol, qrheti);
      // Copy results back into views
      for (Int s = 0; s < Spack::n; ++s) {
        rwdc_device(s).qr = qr[s];
        rwdc_device(s).qcaut = qcaut[s];
        rwdc_device(s).qcacc = qcacc[s];
        rwdc_device(s).qimlt = qimlt[s];
        rwdc_device(s).qcshd = qcshd[s];
        rwdc_device(s).qrevp = qrevp[s];
        rwdc_device(s).qrcol = qrcol[s];
        rwdc_device(s).qrheti = qrheti[s];
      }

    });

    // Sync back to host
    Kokkos::deep_copy(rwdc_host, rwdc_device);

    // Validate results
    for (Int s = 0; s < Spack::n; ++s) {
      REQUIRE(rwdc[s].qr == rwdc_host(s).qr);
      REQUIRE(rwdc[s].qcaut == rwdc_host(s).qcaut);
      REQUIRE(rwdc[s].qcacc == rwdc_host(s).qcacc);
      REQUIRE(rwdc[s].qimlt == rwdc_host(s).qimlt);
      REQUIRE(rwdc[s].qcshd == rwdc_host(s).qcshd);
      REQUIRE(rwdc[s].qrevp == rwdc_host(s).qrevp);
      REQUIRE(rwdc[s].qrcol == rwdc_host(s).qrcol);
      REQUIRE(rwdc[s].qrheti == rwdc_host(s).qrheti);
    }

  }

  static void run_bfb(){

      cloud_water_conservation_unit_bfb_tests();

      rain_water_conservation_unit_bfb_tests();

      ice_water_conservation_unit_bfb_tests();

  }

};

  template <typename D>
  struct UnitWrap::UnitTest<D>::TestP3UpdatePrognosticIce
  {
    static void  update_prognostic_ice_unit_bfb_tests(){

      static constexpr Int max_pack_size = 16;

      REQUIRE(Spack::n <= max_pack_size);

      //fortran generated data is input to the following
      P3UpdatePrognosticIceData pupidc[max_pack_size] = {

	{4.9078E-19, 1.5312E-09, 4.4387E-09, 3.7961E+06, 1.7737E-04, 0.0000E+00, 3.8085E-08, 5.1281E+04, 1.9251E-15,
	 3.4778E-04, 3.5801E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.1386E-07, 0.0000E+00, 0.0000E+00, 2.7053E-02,
	 0.0000E+00, 1.9209E-10, 1.0686E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 4.5312E+02, 2.8720E+02, 5.0000E-03, 6.4286E-05, 1.2344E+08, 7.3684E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 6.4286E-05, 1.0000E-02},

	{2.1097E-18, 2.7648E-09, 3.8261E-09, 3.7754E+06, 6.8685E-04, 0.0000E+00, 4.1018E-08, 5.1227E+04, 4.8876E-15,
	 1.3468E-03, 2.8059E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 7.1049E-07, 0.0000E+00, 0.0000E+00, 2.4547E-02,
	 0.0000E+00, 2.8615E-10, 1.0741E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 3.4890E+02, 2.8642E+02, 5.0000E-03, 7.1429E-05, 1.2345E+08, 7.8947E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.1429E-05, 1.0000E-02},

	{8.9820E-18, 4.2529E-09, 2.9520E-09, 3.7537E+06, 2.6598E-03, 0.0000E+00, 4.3700E-08, 5.1171E+04, 1.4266E-14,
	 5.2153E-03, 1.9880E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 9.0244E-07, 0.0000E+00, 0.0000E+00, 2.1083E-02,
	 0.0000E+00, 3.7631E-10, 1.0796E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.8656E+02, 2.8565E+02, 5.0000E-03, 7.8571E-05, 1.2345E+08, 8.4211E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.8571E-05, 1.0000E-02},

	{3.7942E-17, 6.0115E-09, 1.8004E-09, 3.7310E+06, 1.0300E-02, 0.0000E+00, 4.6119E-08, 5.1112E+04, 4.4518E-14,
	 2.0196E-02, 1.1226E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 1.0879E-06, 0.0000E+00, 0.0000E+00, 1.7646E-02,
	 0.0000E+00, 4.5891E-10, 1.0853E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.4570E+02, 2.8489E+02, 5.0000E-03, 8.5714E-05, 1.2345E+08, 8.9474E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 8.5714E-05, 1.0000E-02},

	{4.9078E-19, 1.5312E-09, 4.4387E-09, 3.7961E+06, 1.7737E-04, 0.0000E+00, 3.8085E-08, 5.1281E+04, 1.9251E-15,
	 3.4778E-04, 3.5801E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.1386E-07, 0.0000E+00, 0.0000E+00, 2.7053E-02,
	 0.0000E+00, 1.9209E-10, 1.0686E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 4.5312E+02, 2.8720E+02, 5.0000E-03, 6.4286E-05, 1.2344E+08, 7.3684E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 6.4286E-05, 1.0000E-02},

	{2.1097E-18, 2.7648E-09, 3.8261E-09, 3.7754E+06, 6.8685E-04, 0.0000E+00, 4.1018E-08, 5.1227E+04, 4.8876E-15,
	 1.3468E-03, 2.8059E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 7.1049E-07, 0.0000E+00, 0.0000E+00, 2.4547E-02,
	 0.0000E+00, 2.8615E-10, 1.0741E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 3.4890E+02, 2.8642E+02, 5.0000E-03, 7.1429E-05, 1.2345E+08, 7.8947E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.1429E-05, 1.0000E-02},

	{8.9820E-18, 4.2529E-09, 2.9520E-09, 3.7537E+06, 2.6598E-03, 0.0000E+00, 4.3700E-08, 5.1171E+04, 1.4266E-14,
	 5.2153E-03, 1.9880E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 9.0244E-07, 0.0000E+00, 0.0000E+00, 2.1083E-02,
	 0.0000E+00, 3.7631E-10, 1.0796E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.8656E+02, 2.8565E+02, 5.0000E-03, 7.8571E-05, 1.2345E+08, 8.4211E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.8571E-05, 1.0000E-02},

	{3.7942E-17, 6.0115E-09, 1.8004E-09, 3.7310E+06, 1.0300E-02, 0.0000E+00, 4.6119E-08, 5.1112E+04, 4.4518E-14,
	 2.0196E-02, 1.1226E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 1.0879E-06, 0.0000E+00, 0.0000E+00, 1.7646E-02,
	 0.0000E+00, 4.5891E-10, 1.0853E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.4570E+02, 2.8489E+02, 5.0000E-03, 8.5714E-05, 1.2345E+08, 8.9474E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 8.5714E-05, 1.0000E-02},

	{4.9078E-19, 1.5312E-09, 4.4387E-09, 3.7961E+06, 1.7737E-04, 0.0000E+00, 3.8085E-08, 5.1281E+04, 1.9251E-15,
	 3.4778E-04, 3.5801E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.1386E-07, 0.0000E+00, 0.0000E+00, 2.7053E-02,
	 0.0000E+00, 1.9209E-10, 1.0686E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 4.5312E+02, 2.8720E+02, 5.0000E-03, 6.4286E-05, 1.2344E+08, 7.3684E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 6.4286E-05, 1.0000E-02},

	{2.1097E-18, 2.7648E-09, 3.8261E-09, 3.7754E+06, 6.8685E-04, 0.0000E+00, 4.1018E-08, 5.1227E+04, 4.8876E-15,
	 1.3468E-03, 2.8059E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 7.1049E-07, 0.0000E+00, 0.0000E+00, 2.4547E-02,
	 0.0000E+00, 2.8615E-10, 1.0741E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 3.4890E+02, 2.8642E+02, 5.0000E-03, 7.1429E-05, 1.2345E+08, 7.8947E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.1429E-05, 1.0000E-02},

	{8.9820E-18, 4.2529E-09, 2.9520E-09, 3.7537E+06, 2.6598E-03, 0.0000E+00, 4.3700E-08, 5.1171E+04, 1.4266E-14,
	 5.2153E-03, 1.9880E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 9.0244E-07, 0.0000E+00, 0.0000E+00, 2.1083E-02,
	 0.0000E+00, 3.7631E-10, 1.0796E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.8656E+02, 2.8565E+02, 5.0000E-03, 7.8571E-05, 1.2345E+08, 8.4211E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.8571E-05, 1.0000E-02},

	{3.7942E-17, 6.0115E-09, 1.8004E-09, 3.7310E+06, 1.0300E-02, 0.0000E+00, 4.6119E-08, 5.1112E+04, 4.4518E-14,
	 2.0196E-02, 1.1226E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 1.0879E-06, 0.0000E+00, 0.0000E+00, 1.7646E-02,
	 0.0000E+00, 4.5891E-10, 1.0853E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.4570E+02, 2.8489E+02, 5.0000E-03, 8.5714E-05, 1.2345E+08, 8.9474E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 8.5714E-05, 1.0000E-02},

	{4.9078E-19, 1.5312E-09, 4.4387E-09, 3.7961E+06, 1.7737E-04, 0.0000E+00, 3.8085E-08, 5.1281E+04, 1.9251E-15,
	 3.4778E-04, 3.5801E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.1386E-07, 0.0000E+00, 0.0000E+00, 2.7053E-02,
	 0.0000E+00, 1.9209E-10, 1.0686E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 4.5312E+02, 2.8720E+02, 5.0000E-03, 6.4286E-05, 1.2344E+08, 7.3684E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 6.4286E-05, 1.0000E-02},

	{2.1097E-18, 2.7648E-09, 3.8261E-09, 3.7754E+06, 6.8685E-04, 0.0000E+00, 4.1018E-08, 5.1227E+04, 4.8876E-15,
	 1.3468E-03, 2.8059E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 7.1049E-07, 0.0000E+00, 0.0000E+00, 2.4547E-02,
	 0.0000E+00, 2.8615E-10, 1.0741E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 3.4890E+02, 2.8642E+02, 5.0000E-03, 7.1429E-05, 1.2345E+08, 7.8947E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.1429E-05, 1.0000E-02},

	{8.9820E-18, 4.2529E-09, 2.9520E-09, 3.7537E+06, 2.6598E-03, 0.0000E+00, 4.3700E-08, 5.1171E+04, 1.4266E-14,
	 5.2153E-03, 1.9880E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 9.0244E-07, 0.0000E+00, 0.0000E+00, 2.1083E-02,
	 0.0000E+00, 3.7631E-10, 1.0796E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.8656E+02, 2.8565E+02, 5.0000E-03, 7.8571E-05, 1.2345E+08, 8.4211E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 7.8571E-05, 1.0000E-02},

	{3.7942E-17, 6.0115E-09, 1.8004E-09, 3.7310E+06, 1.0300E-02, 0.0000E+00, 4.6119E-08, 5.1112E+04, 4.4518E-14,
	 2.0196E-02, 1.1226E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 1.0879E-06, 0.0000E+00, 0.0000E+00, 1.7646E-02,
	 0.0000E+00, 4.5891E-10, 1.0853E+00, 3.3370E+05, 2.8347E+06, true,       true,       1.8000E+03, 2.0000E-01,
	 2.4570E+02, 2.8489E+02, 5.0000E-03, 8.5714E-05, 1.2345E+08, 8.9474E-06, 1.0000E+06, 1.0000E-04, 1.0000E+06,
	 8.5714E-05, 1.0000E-02},
      };


      // Sync to device
      view_1d<P3UpdatePrognosticIceData> pupidc_device("pupidc", Spack::n);
      auto pupidc_host = Kokkos::create_mirror_view(pupidc_device);

      // This copy only copies the input variables.
      std::copy(&pupidc[0], &pupidc[0] + Spack::n, pupidc_host.data());
      Kokkos::deep_copy(pupidc_device, pupidc_host);

      // Get data from fortran
      for (Int i = 0; i < max_pack_size; ++i) {
	update_prognostic_ice(pupidc[i]);
      }

      // Run the lookup from a kernel and copy results back to host
      Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
	  // Init pack inputs
	  Spack qcheti, qccol, qcshd, nccol, ncheti, ncshdc, qrcol, nrcol, qrheti, nrheti, nrshdr,
            qimlt, nimlt, qisub, qidep, qinuc, ninuc, nislf, nisub, qiberg, exner, xlf, xxls,
            nmltratio, rhorime_c, th, qv, qc, nc, qr, nr, qitot, nitot, qirim, birim;
	  Scalar dt;
	  bool log_predictNc, log_wetgrowth;

	  // variables with single values assigned outside of the for loop
	  dt            = pupidc_device(0).dt;
	  log_predictNc = pupidc_device(0).log_predictNc;
	  log_wetgrowth = pupidc_device(0).log_wetgrowth;

	  for (Int s = 0; s < Spack::n; ++s) {

	    qcheti[s] = pupidc_device(s).qcheti;
	    qccol[s]  = pupidc_device(s).qccol;
	    qcshd[s]  = pupidc_device(s).qcshd;
	    nccol[s]  = pupidc_device(s).nccol;
	    ncheti[s] = pupidc_device(s).ncheti;
	    ncshdc[s] = pupidc_device(s).ncshdc;
	    qrcol[s]  = pupidc_device(s).qrcol;
	    nrcol[s]  = pupidc_device(s).nrcol;
	    qrheti[s] = pupidc_device(s).qrheti;
	    nrheti[s] = pupidc_device(s).nrheti;
	    nrshdr[s] = pupidc_device(s).nrshdr;
	    qimlt[s]  = pupidc_device(s).qimlt;
	    nimlt[s]  = pupidc_device(s).nimlt;
	    qisub[s]  = pupidc_device(s).qisub;
	    qidep[s]  = pupidc_device(s).qidep;
	    qinuc[s]  = pupidc_device(s).qinuc;
	    ninuc[s]  = pupidc_device(s).ninuc;
	    nislf[s]  = pupidc_device(s).nislf;
	    nisub[s]  = pupidc_device(s).nisub;
	    qiberg[s] = pupidc_device(s).qiberg;
	    exner[s]  = pupidc_device(s).exner;
	    xlf[s]    = pupidc_device(s).xlf;
	    xxls[s]   = pupidc_device(s).xxls;

	    nmltratio[s] = pupidc_device(s).nmltratio;
	    rhorime_c[s] = pupidc_device(s).rhorime_c;
	    th[s]    = pupidc_device(s).th;
	    qv[s]    = pupidc_device(s).qv;
	    qc[s]    = pupidc_device(s).qc;
	    nc[s]    = pupidc_device(s).nc;
	    qr[s]    = pupidc_device(s).qr;
	    nr[s]    = pupidc_device(s).nr;
	    qitot[s] = pupidc_device(s).qitot;
	    nitot[s] = pupidc_device(s).nitot;
	    qirim[s] = pupidc_device(s).qirim;
	    birim[s] = pupidc_device(s).birim;
	  }

	  Functions::update_prognostic_ice(qcheti, qccol, qcshd, nccol, ncheti,ncshdc,
					   qrcol,   nrcol,  qrheti,  nrheti,  nrshdr,
					   qimlt,  nimlt,  qisub,  qidep,  qinuc,  ninuc,
					   nislf,  nisub,  qiberg,  exner,  xxls,  xlf,
					   log_predictNc, log_wetgrowth,  dt,  nmltratio,
					   rhorime_c, th, qv, qitot, nitot, qirim,
					   birim, qc, nc, qr, nr);

	  // Copy results back into views
	  pupidc_device(0).dt            = dt;
	  pupidc_device(0).log_predictNc = log_predictNc;
	  pupidc_device(0).log_wetgrowth = log_wetgrowth;
	  for (Int s = 0; s < Spack::n; ++s) {

	    pupidc_device(s).qcheti = qcheti[s];
	    pupidc_device(s).qccol  = qccol[s];
	    pupidc_device(s).qcshd  = qcshd[s];
	    pupidc_device(s).nccol  = nccol[s];
	    pupidc_device(s).ncheti = ncheti[s];
	    pupidc_device(s).ncshdc = ncshdc[s];
	    pupidc_device(s).qrcol  = qrcol[s];
	    pupidc_device(s).nrcol  = nrcol[s];
	    pupidc_device(s).qrheti = qrheti[s];
	    pupidc_device(s).nrheti = nrheti[s];
	    pupidc_device(s).nrshdr = nrshdr[s];
	    pupidc_device(s).qimlt  = qimlt[s];
	    pupidc_device(s).nimlt  = nimlt[s];
	    pupidc_device(s).qisub  = qisub[s];
	    pupidc_device(s).qidep  = qidep[s];
	    pupidc_device(s).qinuc  = qinuc[s];
	    pupidc_device(s).ninuc  = ninuc[s];
	    pupidc_device(s).nislf  = nislf[s];
	    pupidc_device(s).nisub  = nisub[s];
	    pupidc_device(s).qiberg = qiberg[s];
	    pupidc_device(s).exner  = exner[s];
	    pupidc_device(s).xlf    = xlf[s];
	    pupidc_device(s).xxls   = xxls[s];

	    pupidc_device(s).nmltratio = nmltratio[s];
	    pupidc_device(s).rhorime_c = rhorime_c[s];
	    pupidc_device(s).th    = th[s];
	    pupidc_device(s).qv	   = qv[s];
	    pupidc_device(s).qc	   = qc[s];
	    pupidc_device(s).nc	   = nc[s];
	    pupidc_device(s).qr	   = qr[s];
	    pupidc_device(s).nr    = nr[s];
	    pupidc_device(s).qitot = qitot[s];
	    pupidc_device(s).nitot = nitot[s];
	    pupidc_device(s).qirim = qirim[s];
	    pupidc_device(s).birim = birim[s];
	  }

	});

      // Sync back to host
      Kokkos::deep_copy(pupidc_host, pupidc_device);

      // Validate results
      for (Int s = 0; s < Spack::n; ++s) {
	REQUIRE(pupidc[s].qc    == pupidc_host(s).qc);
	REQUIRE(pupidc[s].nr    == pupidc_host(s).nr);
	REQUIRE(pupidc[s].qr    == pupidc_host(s).qr);
	REQUIRE(pupidc[s].qv    == pupidc_host(s).qv);
	REQUIRE(pupidc[s].nc    == pupidc_host(s).nc);
	REQUIRE(pupidc[s].qitot == pupidc_host(s).qitot);
	REQUIRE(pupidc[s].nitot == pupidc_host(s).nitot);
	REQUIRE(pupidc[s].qirim == pupidc_host(s).qirim);
	REQUIRE(pupidc[s].birim == pupidc_host(s).birim );
	REQUIRE(pupidc[s].th    == pupidc_host(s).th);
	}
    }

    static void run_bfb(){
      update_prognostic_ice_unit_bfb_tests();
    }

  };//TestP3UpdatePrognosticIce

  template <typename D>
  struct UnitWrap::UnitTest<D>::TestP3UpdatePrognosticLiq
  {
    static void  update_prognostic_liquid_unit_bfb_tests(){

      static constexpr Int max_pack_size = 16;

      REQUIRE(Spack::n <= max_pack_size);

      //fortran generated data is input to the following
      P3UpdatePrognosticLiqData pupldc[max_pack_size] = {

	{1.0631E-12, 1.0631E+00, 1.5833E-12, 1.5833E+00, 0.0000E+00, 2.4190E-02, 0.0000E+00, 0.0000E+00, 0.0000E+00, 4.2517E+00,
	 true      , 8.6718E-01, 1.0037E+00, 2.5010E+06, 1.8000E+03, 2.9902E+02, 5.0000E-02, 1.0000E-06, 1.0000E+06, 1.0010E-06,
	 6.3726E+05},

	{3.2784E-08, 1.8780E+07, 2.1753E-11, 1.2461E+04, 0.0000E+00, 7.8657E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.8748E+04,
	 true      , 9.8387E-01, 1.0741E+00, 2.5010E+06, 1.8000E+03, 2.9033E+02, 3.7211E-03, 5.9050E-05,-6.6723E+09,-5.9050E-05,
	 -8.6159E+07},

	{3.2796E-09, 1.8778E+07, 1.8830E-12, 1.0782E+04, 0.0000E+00, 6.8061E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.3698E+04,
	 true      , 9.0740E-01, 1.0293E+00, 2.5010E+06, 1.8000E+03, 2.9376E+02, 5.0000E-03, 5.9067E-06,-6.9543E+09, 1.0439E-04,
	 -1.6967E+07},

	{6.5634E-09, 1.8778E+07, 3.8238E-12, 1.0940E+04, 0.0000E+00, 6.9061E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.3181E+04,
	 true      , 9.1484E-01, 1.0339E+00, 2.5010E+06, 1.8000E+03, 2.9291E+02, 5.0000E-03, 1.1821E-05,-6.9282E+09, 1.0615E-04,
	 -2.8223E+07},

	{9.8516E-09, 1.8779E+07, 5.8258E-12, 1.1105E+04, 0.0000E+00, 7.0101E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.2655E+04,
	 true      , 9.2251E-01, 1.0386E+00, 2.5010E+06, 1.8000E+03, 2.9206E+02, 5.0000E-03, 1.7743E-05,-6.9009E+09, 1.0790E-04,
	 -3.9628E+07},

	{1.3145E-08, 1.8779E+07, 7.8929E-12, 1.1276E+04, 0.0000E+00, 7.1180E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.2122E+04,
	 true      , 9.3043E-01, 1.0433E+00, 2.5010E+06, 1.8000E+03, 2.9123E+02, 5.0000E-03, 2.3674E-05,-6.8725E+09, 1.0963E-04,
	 -5.1189E+07},

	{1.6443E-08, 1.8779E+07, 1.0029E-11, 1.1454E+04, 0.0000E+00, 7.2303E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.1581E+04,
	 true      , 9.3860E-01, 1.0482E+00, 2.5010E+06, 1.8000E+03, 2.9040E+02, 5.0000E-03, 2.9615E-05,-6.8428E+09, 1.1136E-04,
	 -6.2915E+07},

	{1.9746E-08, 1.8779E+07, 1.2238E-11, 1.1639E+04, 0.0000E+00, 7.3471E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.1031E+04,
	 true      , 9.4705E-01, 1.0531E+00, 2.5010E+06, 1.8000E+03, 2.8958E+02, 5.0000E-03, 3.5565E-05,-6.8117E+09, 1.1308E-04,
	 -7.4813E+07},

	{2.3047E-08, 1.8779E+07, 1.4521E-11, 1.1832E+04, 0.0000E+00, 7.4688E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 6.0474E+04,
	 true      , 9.5579E-01, 1.0582E+00, 2.5010E+06, 1.8000E+03, 2.8941E+02, 4.7949E-03, 4.1510E-05,-6.7792E+09, 1.4787E-05,
	 -8.2885E+07},

	{2.6289E-08, 1.8779E+07, 1.6845E-11, 1.2033E+04, 0.0000E+00, 7.5955E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.9907E+04,
	 true      , 9.6483E-01, 1.0634E+00, 2.5010E+06, 1.8000E+03, 2.8972E+02, 4.4341E-03, 4.7350E-05,-6.7452E+09,-4.7350E-05,
	 -8.3634E+07},

	{2.9533E-08, 1.8779E+07, 1.9253E-11, 1.2242E+04, 0.0000E+00, 7.7277E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.9332E+04,
	 true      , 9.7418E-01, 1.0686E+00, 2.5010E+06, 1.8000E+03, 2.9002E+02, 4.0751E-03, 5.3194E-05,-6.7096E+09,-5.3194E-05,
	 -8.4862E+07},

	{3.2784E-08, 1.8780E+07, 2.1753E-11, 1.2461E+04, 0.0000E+00, 7.8657E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.8748E+04,
	 true      , 9.8387E-01, 1.0741E+00, 2.5010E+06, 1.8000E+03, 2.9033E+02, 3.7211E-03, 5.9050E-05,-6.6723E+09,-5.9050E-05,
	 -8.6159E+07},

	{3.6045E-08, 1.8780E+07, 2.4356E-11, 1.2689E+04, 0.0000E+00, 8.0098E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.8154E+04,
	 true      , 9.9391E-01, 1.0796E+00, 2.5010E+06, 1.8000E+03, 2.9063E+02, 3.3756E-03, 6.4925E-05,-6.6333E+09,-6.4925E-05,
	 -8.7530E+07},

	{3.9321E-08, 1.8780E+07, 2.7069E-11, 1.2928E+04, 0.0000E+00, 8.1605E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.7552E+04,
	 true      , 1.0043E+00, 1.0853E+00, 2.5010E+06, 1.8000E+03, 2.9092E+02, 3.0417E-03, 7.0827E-05,-6.5924E+09,-7.0827E-05,
	 -8.8982E+07},

	{4.2614E-08, 1.8780E+07, 2.9903E-11, 1.3178E+04, 0.0000E+00, 8.3182E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.6939E+04,
	 true      , 1.0151E+00, 1.0911E+00, 2.5010E+06, 1.8000E+03, 2.9119E+02, 2.7224E-03, 7.6760E-05,-6.5494E+09,-7.6760E-05,
	 -9.0523E+07},

	{4.5927E-08, 1.8780E+07, 3.2867E-11, 1.3440E+04, 0.0000E+00, 8.4833E+03, 0.0000E+00, 0.0000E+00, 0.0000E+00, 5.6317E+04,
	 true      , 1.0263E+00, 1.0970E+00, 2.5010E+06, 1.8000E+03, 2.9143E+02, 2.4202E-03, 8.2728E-05,-6.5044E+09,-8.2728E-05,
	 -9.0778E+07},
      };

      // Sync to device
      view_1d<P3UpdatePrognosticLiqData> pupldc_device("pupldc", Spack::n);
      auto pupldc_host = Kokkos::create_mirror_view(pupldc_device);

      // This copy only copies the input variables.
      std::copy(&pupldc[0], &pupldc[0] + Spack::n, pupldc_host.data());
      Kokkos::deep_copy(pupldc_device, pupldc_host);

      // Get data from fortran
      for (Int i = 0; i < max_pack_size; ++i) {
        update_prognostic_liquid(pupldc[i]);
      }

      // Run the lookup from a kernel and copy results back to host
      Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {

	  // Init pack inputs
	  Spack qcacc, ncacc, qcaut, ncautc, qcnuc, ncautr, ncslf, qrevp, nrevp, nrslf, inv_rho,
	    exner, xxlv, th, qv, qc, nc, qr, nr;
	  bool log_predictNc;
	  Scalar dt;

	  // variables with single values assigned outside of the for loop
	  dt            = pupldc_device(0).dt;
	  log_predictNc = pupldc_device(0).log_predictNc;

	  for (Int s = 0; s < Spack::n; ++s) {
	    qcacc[s]   = pupldc_device(s).qcacc;
	    ncacc[s]   = pupldc_device(s).ncacc;
	    qcaut[s]   = pupldc_device(s).qcaut;
	    ncautc[s]  = pupldc_device(s).ncautc;
	    qcnuc[s]   = pupldc_device(s).qcnuc;
	    ncautr[s]  = pupldc_device(s).ncautr;
	    ncslf[s]   = pupldc_device(s).ncslf;
	    qrevp[s]   = pupldc_device(s).qrevp;
	    nrevp[s]   = pupldc_device(s).nrevp;
	    nrslf[s]   = pupldc_device(s).nrslf;
	    inv_rho[s] = pupldc_device(s).inv_rho;
	    exner[s]   = pupldc_device(s).exner;
	    xxlv[s]    = pupldc_device(s).xxlv;

	    th[s]      = pupldc_device(s).th;
	    qv[s]      = pupldc_device(s).qv;
	    qc[s]      = pupldc_device(s).qc;
	    nc[s]      = pupldc_device(s).nc;
	    qr[s]      = pupldc_device(s).qr;
	    nr[s]      = pupldc_device(s).nr;
	  }

	  Functions::update_prognostic_liquid(qcacc, ncacc, qcaut, ncautc, qcnuc, ncautr, ncslf,
					      qrevp, nrevp, nrslf, log_predictNc, inv_rho, exner,
					      xxlv, dt, th, qv, qc, nc, qr, nr);

	  // Copy results back into views
	  pupldc_device(0).dt            = dt;
	  pupldc_device(0).log_predictNc = log_predictNc;

	  for (Int s = 0; s < Spack::n; ++s) {
	    pupldc_device(s).qcacc   = qcacc[s];
	    pupldc_device(s).ncacc   = ncacc[s];
	    pupldc_device(s).qcaut   = qcaut[s];
	    pupldc_device(s).ncautc  = ncautc[s];
	    pupldc_device(s).qcnuc   = qcnuc[s];
	    pupldc_device(s).ncautr  = ncautr[s];
	    pupldc_device(s).ncslf   = ncslf[s];
	    pupldc_device(s).qrevp   = qrevp[s];
	    pupldc_device(s).nrevp   = nrevp[s];
	    pupldc_device(s).nrslf   = nrslf[s];
	    pupldc_device(s).inv_rho = inv_rho[s];
	    pupldc_device(s).exner   = exner[s];
	    pupldc_device(s).xxlv    = xxlv[s];

	    pupldc_device(s).th      = th[s];
	    pupldc_device(s).qv      = qv[s];
	    pupldc_device(s).qc      = qc[s];
	    pupldc_device(s).nc      = nc[s];
	    pupldc_device(s).qr      = qr[s];
	    pupldc_device(s).nr      = nr[s];

	  }
	});

      // Sync back to host
      Kokkos::deep_copy(pupldc_host, pupldc_device);

      // Validate results
      for (Int s = 0; s < Spack::n; ++s) {
        REQUIRE(pupldc[s].th == pupldc_host(s).th);
        REQUIRE(pupldc[s].qv == pupldc_host(s).qv);
        REQUIRE(pupldc[s].qc == pupldc_host(s).qc);
        REQUIRE(pupldc[s].nc == pupldc_host(s).nc);
        REQUIRE(pupldc[s].qr == pupldc_host(s).qr);
        REQUIRE(pupldc[s].nr == pupldc_host(s).nr);

      }
    }

    static void run_bfb(){
      update_prognostic_liquid_unit_bfb_tests();
    }

  }; //TestP3UpdatePrognosticLiq

  template <typename D>
  struct UnitWrap::UnitTest<D>::TestP3FunctionsImposeMaxTotalNi
  {
    static void impose_max_total_ni_bfb_test(){
      static constexpr Int max_pack_size = 16;
      REQUIRE(Spack::n <= max_pack_size);

      ImposeMaxTotalNiData dc[max_pack_size]= {
        // nitot_local, max_total_Ni, inv_rho_local
        {0.000E0, 5.000E5, 5.466E3},
        {3.358E4, 5.000E5, 9.691E-1},
        {0.000E0, 5.000E5, 9.105E-1},
        {0.000E3, 5.000E5, 3.371E0},

        {0.000E0, 5.000E5, 5.466E3},
        {3.358E4, 5.000E5, 9.691E-1},
        {0.000E0, 5.000E5, 9.105E-1},
        {0.000E3, 5.000E5, 3.371E0},

        {0.000E0, 5.000E5, 5.466E3},
        {3.358E4, 5.000E5, 9.691E-1},
        {0.000E0, 5.000E5, 9.105E-1},
        {0.000E3, 5.000E5, 3.371E0},

        {0.000E0, 5.000E5, 5.466E3},
        {3.358E4, 5.000E5, 9.691E-1},
        {0.000E0, 5.000E5, 9.105E-1},
        {0.000E3, 5.000E5, 3.371E0},

      };

      //Sync to device
      view_1d<ImposeMaxTotalNiData> dc_device("dc", Spack::n);
      auto dc_host = Kokkos::create_mirror_view(dc_device);

      //This copy only copies the input variables.
      std::copy(&dc[0], &dc[0] + Spack::n, dc_host.data());
      Kokkos::deep_copy(dc_device, dc_host);

      //Get data from fortran
      for (Int i = 0; i < Spack::n; ++i) {
        impose_max_total_Ni(dc[i]);
      }

      //Run function from a kernal and copy results back to the host
      Kokkos::parallel_for(RangePolicy(0, 1), KOKKOS_LAMBDA(const Int& i) {
	  // Init pack inputs
	  Spack nitot_local, max_total_Ni, inv_rho_local;
	  for (Int s = 0; s < Spack::n; ++s) {
	    nitot_local[s] = dc_device(s).nitot_local;
	    max_total_Ni[s] = dc_device(s).max_total_Ni;
	    inv_rho_local[s] = dc_device(s).inv_rho_local;
	  }

	  Functions::impose_max_total_Ni(nitot_local, max_total_Ni, inv_rho_local);
	  // Copy results back into views
	  for (Int s = 0; s < Spack::n; ++s) {
	    dc_device(s).nitot_local = nitot_local[s];
	    dc_device(s).max_total_Ni = max_total_Ni[s];
	    dc_device(s).inv_rho_local = inv_rho_local[s];
	  }
        });

      // Sync back to host
      Kokkos::deep_copy(dc_host, dc_device);

      // Validate results
      for (Int s = 0; s < Spack::n; ++s) {
        REQUIRE(dc[s].nitot_local == dc_host(s).nitot_local);
        REQUIRE(dc[s].max_total_Ni == dc_host(s).max_total_Ni);
        REQUIRE(dc[s].inv_rho_local == dc_host(s).inv_rho_local);
      }
    }

 static void run_bfb(){
   impose_max_total_ni_bfb_test();
 }

}; // TestP3FunctionsImposeMaxTotalNi

}//namespace unit_test
}//namespace p3
}//namespace scream

namespace {

TEST_CASE("p3_functions", "[p3_functions]")
{
  scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestP3Func::run();
}

TEST_CASE("p3_conservation_test", "[p3_conservation_test]"){
  scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestP3Conservation::run();
  scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestP3Conservation::run_bfb();
}

TEST_CASE("p3_update_prognostic_ice_test", "[p3_update_prognostic_ice_test]"){
  scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestP3UpdatePrognosticIce::run_bfb();
}

TEST_CASE("p3_update_prognostic_liquid_test", "[p3_update_prognostic_liquid_test]"){
  scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestP3UpdatePrognosticLiq::run_bfb();
}

TEST_CASE("p3_impose_max_total_ni_test", "[p3_impose_max_total_ni_test]"){
  scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestP3FunctionsImposeMaxTotalNi::run_bfb();
}

} // namespace
