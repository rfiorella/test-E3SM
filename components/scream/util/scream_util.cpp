#include "scream_util.hpp"
#include "scream_types.hpp"
#include "scream_kokkos.hpp"

#include <sstream>

#ifdef _OPENMP
# include <omp.h>
#endif

#ifdef SCREAM_FPE
# include <xmmintrin.h>
#endif

namespace scream {
namespace util {

void activate_floating_point_exceptions_if_enabled () {
#ifdef SCREAM_FPE
  _MM_SET_EXCEPTION_MASK(_MM_GET_EXCEPTION_MASK() &
                         ~( _MM_MASK_INVALID |
                            _MM_MASK_DIV_ZERO |
                            _MM_MASK_OVERFLOW |
                            _MM_MASK_UNDERFLOW ));
#endif
}

void initialize (int argc, char **argv) {
  activate_floating_point_exceptions_if_enabled();
  Kokkos::initialize(argc, argv);
  std::cout << config_string() << "\n";
}

void finalize () {
  Kokkos::finalize();
}

std::string active_avx_string () {
  std::string s;
#if defined __AVX512F__
  s += "-AVX512F";
#endif
#if defined __AVX2__
  s += "-AVX2";
#endif
#if defined __AVX__
  s += "-AVX";
#endif
  return s;
}

std::string config_string () {
  std::stringstream ss;
  ss << "sizeof(Real) " << sizeof(Real)
     << " avx " << active_avx_string()
     << " compiler " <<
#if defined __INTEL_COMPILER
    "Intel"
#elif defined __GNUG__
    "GCC"
#else
    "unknown"
#endif
     << " FPE " <<
#ifdef SCREAM_FPE
    "on"
#else
    "off"
#endif
     << " #threads " <<
#ifdef _OPENMP
    omp_get_max_threads()
#else
    1
#endif
    ;
  return ss.str();
}

bool eq (const std::string& a, const char* const b1, const char* const b2) {
  return (a == std::string(b1) || (b2 && a == std::string(b2)) ||
          a == std::string("-") + std::string(b1));

}

} // namespace util
} // namespace scream
