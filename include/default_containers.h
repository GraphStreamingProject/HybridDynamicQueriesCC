#pragma once

#ifdef USE_VECTOR_DEFAULT_CONTAINER
inline constexpr bool use_vector_default_container = true;
#else
inline constexpr bool use_vector_default_container = false;
#endif
