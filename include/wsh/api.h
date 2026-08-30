/**
 * @file api.h
 * @brief Embedding ABI 1 export marker.
 *
 * WSH_API marks the public embedding surface. When the shared library is
 * built (WSH_SHARED_BUILD), the marked functions are the only symbols the DLL
 * exports; every other symbol stays internal. Static consumers and the
 * executable see an empty marker and link the surface directly. Consumers of
 * the shared library resolve the surface through its import definition and do
 * not require an import marker.
 */

#ifndef WSH_API_H
#define WSH_API_H

#if defined(WSH_SHARED_BUILD)
#  define WSH_API __declspec(dllexport)
#else
#  define WSH_API
#endif

#endif
