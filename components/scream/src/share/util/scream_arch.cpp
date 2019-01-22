#ifdef FPE
# include <xmmintrin.h>
#endif

#ifdef _OPENMP
# include <omp.h>
#endif

#include <sstream>

#include "share/util/scream_arch.hpp"
#include "share/scream_types.hpp"
#include "share/scream_config.hpp"

/*
 * Implementations of scream_arch.hpp functions.
 */

namespace scream {
namespace util {

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
     << " packsize " << SCREAM_PACK_SIZE
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
#ifdef KOKKOS_ENABLE_OPENMP
         Kokkos::OpenMP::concurrency()
#elif defined _OPENMP
         omp_get_max_threads()
#else
         1
#endif
    ;
  return ss.str();
}

#ifdef SCREAM_FPE
static unsigned int constexpr exceptions =
  _MM_MASK_INVALID |
  _MM_MASK_DIV_ZERO |
  _MM_MASK_OVERFLOW;
#endif

void activate_floating_point_exceptions_if_enabled () {
#ifdef SCREAM_FPE
  _MM_SET_EXCEPTION_MASK(_MM_GET_EXCEPTION_MASK() & ~exceptions);
#endif
}

void deactivate_floating_point_exceptions_if_enabled () {
#ifdef SCREAM_FPE
  _MM_SET_EXCEPTION_MASK(_MM_GET_EXCEPTION_MASK() | exceptions);
#endif
}

} // namespace util
} // namespace scream
