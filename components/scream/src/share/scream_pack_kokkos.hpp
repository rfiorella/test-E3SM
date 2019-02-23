#ifndef INCLUDE_SCREAM_PACK_KOKKOS
#define INCLUDE_SCREAM_PACK_KOKKOS

#include "scream_pack.hpp"
#include "scream_kokkos_meta.hpp"

namespace scream {
namespace pack {

/* These functions combine Pack, Mask, and Kokkos::Views.
 */

// Index a scalar array with Pack indices, returning a compatible Pack of array
// values.
template<typename Array1, typename IdxPack> KOKKOS_INLINE_FUNCTION
OnlyPackReturn<IdxPack, Pack<typename Array1::non_const_value_type, IdxPack::n> >
index (const Array1& a, const IdxPack& i0,
       typename std::enable_if<Array1::Rank == 1>::type* = nullptr) {
  Pack<typename Array1::non_const_value_type, IdxPack::n> p;
  vector_simd for (int i = 0; i < IdxPack::n; ++i)
    p[i] = a(i0[i]);
  return p;
}

template<typename Array2, typename IdxPack> KOKKOS_INLINE_FUNCTION
OnlyPackReturn<IdxPack, Pack<typename Array2::non_const_value_type, IdxPack::n> >
index (const Array2& a, const IdxPack& i0, const IdxPack& i1,
       typename std::enable_if<Array2::Rank == 2>::type* = nullptr) {
  Pack<typename Array2::non_const_value_type, IdxPack::n> p;
  vector_simd for (int i = 0; i < IdxPack::n; ++i)
    p[i] = a(i0[i], i1[i]);
  return p;
}

// Turn a View of Packs into a View of scalars.
// Example: const auto b = scalarize(a);
template <typename T, typename ...Parms, int pack_size> KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<T**, Parms...> >
scalarize (const Kokkos::View<Pack<T, pack_size>**, Parms...>& vp) {
  return ko::Unmanaged<Kokkos::View<T**, Parms...> >(
    reinterpret_cast<T*>(vp.data()), vp.extent_int(0), pack_size * vp.extent_int(1));
}

template <typename T, typename ...Parms, int pack_size> KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<T*, Parms...> >
scalarize (const Kokkos::View<Pack<T, pack_size>*, Parms...>& vp) {
  return ko::Unmanaged<Kokkos::View<T*, Parms...> >(
    reinterpret_cast<T*>(vp.data()), pack_size * vp.extent_int(0));
}

// Turn a View of Pack<T,N>s into a View of Pack<T,M>s. M must divide N:
//     N % M == 0.
// Example: const auto b = repack<4>(a);
template <int new_pack_size,
          typename T, typename ...Parms, int old_pack_size>
KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<Pack<T, new_pack_size>**, Parms...> >
repack (const Kokkos::View<Pack<T, old_pack_size>**, Parms...>& vp) {
  static_assert(new_pack_size > 0 &&
                old_pack_size % new_pack_size == 0,
                "New pack size must divide old pack size.");
  return ko::Unmanaged<Kokkos::View<Pack<T, new_pack_size>**, Parms...> >(
    reinterpret_cast<Pack<T, new_pack_size>*>(vp.data()),
    vp.extent_int(0),
    (old_pack_size / new_pack_size) * vp.extent_int(1));
}

// shrinking
template <int new_pack_size,
          typename T, typename ...Parms, int old_pack_size,
          typename std::enable_if<(old_pack_size >= new_pack_size), int>::type = 0>
KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<Pack<T, new_pack_size>*, Parms...> >
repack (const Kokkos::View<Pack<T, old_pack_size>*, Parms...>& vp) {
  static_assert(new_pack_size > 0 &&
                old_pack_size % new_pack_size == 0,
                "New pack size must divide old pack size.");
  return ko::Unmanaged<Kokkos::View<Pack<T, new_pack_size>*, Parms...> >(
    reinterpret_cast<Pack<T, new_pack_size>*>(vp.data()),
    (old_pack_size / new_pack_size) * vp.extent_int(0));
}

// growing
template <int new_pack_size,
          typename T, typename ...Parms, int old_pack_size,
          typename std::enable_if<(old_pack_size < new_pack_size), int>::type = 0>
KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<Pack<T, new_pack_size>*, Parms...> >
repack (const Kokkos::View<Pack<T, old_pack_size>*, Parms...>& vp) {
  static_assert(new_pack_size > 0 &&
                new_pack_size % old_pack_size == 0,
                "Old pack size must divide new pack size.");
  scream_kassert(vp.extent_int(0) % (new_pack_size / old_pack_size) == 0);
  return ko::Unmanaged<Kokkos::View<Pack<T, new_pack_size>*, Parms...> >(
    reinterpret_cast<Pack<T, new_pack_size>*>(vp.data()),
    vp.extent_int(0) / (new_pack_size / old_pack_size));
}

template <typename T>
using BigPack = Pack<T, SCREAM_PACK_SIZE>;
template <typename T>
using SmallPack = Pack<T, SCREAM_SMALL_PACK_SIZE>;
using IntSmallPack = SmallPack<Int>;

template <typename T, typename ...Parms> KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<SmallPack<T>**, Parms...> >
smallize (const Kokkos::View<BigPack<T>**, Parms...>& vp) {
  return repack<SCREAM_SMALL_PACK_SIZE>(vp);
}

template <typename T, typename ...Parms> KOKKOS_FORCEINLINE_FUNCTION
ko::Unmanaged<Kokkos::View<SmallPack<T>*, Parms...> >
smallize (const Kokkos::View<BigPack<T>*, Parms...>& vp) {
  return repack<SCREAM_SMALL_PACK_SIZE>(vp);
}

} // namespace pack
} // namespace scream

#endif // INCLUDE_SCREAM_PACK_KOKKOS
