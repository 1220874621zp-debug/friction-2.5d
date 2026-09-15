// Force-included (via /FI) when building the skottie modules with
// MSVC: skia is normally built with clang-cl/libc++, where the std
// headers pull each other in more generously; some skia private
// headers (e.g. SkPathRef.h uses std::tuple) rely on that and do not
// include what they use.
#ifndef FRICTIONSKOTTIE_MSVC_COMPAT_H
#define FRICTIONSKOTTIE_MSVC_COMPAT_H

#include <tuple>
#include <type_traits>
#include <utility>
#include <memory>
#include <functional>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstddef>

#endif
