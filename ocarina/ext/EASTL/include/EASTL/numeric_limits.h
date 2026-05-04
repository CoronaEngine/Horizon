///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Electronic Arts Inc. All rights reserved.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// We support eastl::numeric_limits for the following types. Sized types such
// as int32_t are covered by these basic types, with the exception of int128_t.
//
//     bool
//     char                 (distinct from signed and unsigned char)
//     unsigned char,
//     signed char, 
//     wchar_t
//     char16_t             (when char16_t is a distict type)
//     char32_t             (when char32_t is a distinct type)
//     unsigned short,
//     signed short
//     unsigned int
//     signed int
//     unsigned long
//     signed long
//     signed long long
//     unsigned long long
//     uint128_t            (when supported natively by the compiler)
//     int128_t             (when supported natively by the compiler)
//     float
//     double
//     long double
///////////////////////////////////////////////////////////////////////////////


#ifndef EASTL_NUMERIC_LIMITS_H
#define EASTL_NUMERIC_LIMITS_H


#include <EASTL/internal/config.h>
#include <EASTL/type_traits.h>
#include <limits.h>                 // C limits.h header
#include <float.h>
#if defined(_CPPLIB_VER)            // Dinkumware.
	#include <ymath.h>
#endif

#if defined(EA_PRAGMA_ONCE_SUPPORTED)
	#pragma once // Some compilers (e.g. VC++) benefit significantly from using this. We've measured 3-4% build speed improvements in apps as a result.
#endif


// Disable Warnings:
//   4310 - cast truncates constant value
//   4296 - expression is always false
EA_DISABLE_VC_WARNING(4310 4296)

// EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED
//
// Defined as 0 or 1.
// Indicates whether we need to define our own implementations of inf, nan, snan, denorm floating point constants. 
//
#if !defined(EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED)
	#if (defined(EA_COMPILER_GNUC) || defined(__clang__) && defined(__FLT_MIN__)) || defined(_CPPLIB_VER) // __FLT_MIN__ detects if it's really GCC/clang and not a mimic. _CPPLIB_VER (Dinkumware) covers VC++, and Microsoft platforms.
		#define EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED 0
	#else
		#define EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED 1
	#endif
#endif


///////////////////////////////////////////////////////////////////////////////
// min/max workaround
//
// MSVC++ has #defines for min/max which collide with the min/max algorithm
// declarations. The following may still not completely resolve some kinds of
// problems with MSVC++ #defines, though it deals with most cases in production
// game code.
//
#if EASTL_NOMINMAX
	#ifdef min
		#undef min
	#endif
	#ifdef max
		#undef max
	#endif
#endif


// EA_CONSTEXPR
// EA_CONSTEXPR is defined in EABase 2.00.38 and later.
#if !defined(EA_CONSTEXPR)
	#define EA_CONSTEXPR
#endif

// EA_CONSTEXPR_OR_CONST
// EA_CONSTEXPR_OR_CONST is defined in EABase 2.00.39 and later.
#if !defined(EA_CONSTEXPR_OR_CONST)
	#define EA_CONSTEXPR_OR_CONST const
#endif


///////////////////////////////////////////////////////////////////////////////
// EASTL_LIMITS macros
// These apply to integral types only.
///////////////////////////////////////////////////////////////////////////////

// true or false.
#define EASTL_LIMITS_IS_SIGNED(T)    ((T)(-1) < 0)

// The min possible value of T. 
#define EASTL_LIMITS_MIN_S(T) ((T)((T)1 << EASTL_LIMITS_DIGITS_S(T)))
#define EASTL_LIMITS_MIN_U(T) ((T)0)
#define EASTL_LIMITS_MIN(T)   ((EASTL_LIMITS_IS_SIGNED(T) ? EASTL_LIMITS_MIN_S(T) : EASTL_LIMITS_MIN_U(T)))

// The max possible value of T. 
#define EASTL_LIMITS_MAX_S(T) ((T)(((((T)1 << (EASTL_LIMITS_DIGITS(T) - 1)) - 1) << 1) + 1))
#define EASTL_LIMITS_MAX_U(T) ((T)~(T)0)
#define EASTL_LIMITS_MAX(T)   ((EASTL_LIMITS_IS_SIGNED(T) ? EASTL_LIMITS_MAX_S(T) : EASTL_LIMITS_MAX_U(T)))

// The number of bits in the representation of T.
#define EASTL_LIMITS_DIGITS_S(T) ((sizeof(T) * CHAR_BIT) - 1)
#define EASTL_LIMITS_DIGITS_U(T) ((sizeof(T) * CHAR_BIT))
#define EASTL_LIMITS_DIGITS(T)   ((EASTL_LIMITS_IS_SIGNED(T) ? EASTL_LIMITS_DIGITS_S(T) : EASTL_LIMITS_DIGITS_U(T)))

// The number of decimal digits that can be represented by T.
#define EASTL_LIMITS_DIGITS10_S(T) ((EASTL_LIMITS_DIGITS_S(T) * 643L) / 2136) // (643 / 2136) ~= log10(2).
#define EASTL_LIMITS_DIGITS10_U(T) ((EASTL_LIMITS_DIGITS_U(T) * 643L) / 2136)
#define EASTL_LIMITS_DIGITS10(T)   ((EASTL_LIMITS_IS_SIGNED(T) ? EASTL_LIMITS_DIGITS10_S(T) : EASTL_LIMITS_DIGITS10_U(T)))






namespace eastl
{
	// See C++11 18.3.2.5
	enum float_round_style
	{
		round_indeterminate       = -1,    /// Intermediate.
		round_toward_zero         =  0,    /// To zero.
		round_to_nearest          =  1,    /// To the nearest representable value.
		round_toward_infinity     =  2,    /// To infinity.
		round_toward_neg_infinity =  3     /// To negative infinity.
	};

	// See C++11 18.3.2.6
	enum float_denorm_style
	{
		denorm_indeterminate = -1,          /// It cannot be determined whether or not the type allows denormalized values.
		denorm_absent        =  0,          /// The type does not allow denormalized values.
		denorm_present       =  1           /// The type allows denormalized values.
	};


	namespace Internal
	{
		// Defines default values for numeric_limits, which can be overridden by class specializations.
		// See C++11 18.3.2.3
		struct numeric_limits_base
		{
			// true if the type has an explicit specialization defined in the template class; false if not. 
			static EA_CONSTEXPR_OR_CONST bool is_specialized = false;

			// Integer types: the number of *bits* in the representation of T.
			// Floating types: the number of digits in the mantissa of T (same as FLT_MANT_DIG, DBL_MANT_DIG or LDBL_MANT_DIG).
			static EA_CONSTEXPR_OR_CONST int digits = 0;

			// The number of decimal digits that can be represented by T.
			// Equivalent to FLT_DIG, DBL_DIG or LDBL_DIG for floating types.
			static EA_CONSTEXPR_OR_CONST int digits10 = 0;

			// The number of decimal digits required to make sure that two distinct values of the type have distinct decimal representations.
			static EA_CONSTEXPR_OR_CONST int max_digits10 = 0;

			// True if the type is signed.
			static EA_CONSTEXPR_OR_CONST bool is_signed = false;

			// True if the type is integral.
			static EA_CONSTEXPR_OR_CONST bool is_integer = false;

			// True if the type uses an exact representation. All integral types are
			// exact, but other types can be exact as well.
			static EA_CONSTEXPR_OR_CONST bool is_exact = false;

			// Integer types: the base of the representation. Always 2 for integers.
			// Floating types: the base of the exponent representation. Always FLT_RADIX (typically 2) for float.
			static EA_CONSTEXPR_OR_CONST int radix = 0;

			// The minimum integral radix-based exponent representable by the type.
			static EA_CONSTEXPR_OR_CONST int min_exponent = 0;

			// The minimum integral base 10 exponent representable by the type.
			static EA_CONSTEXPR_OR_CONST int min_exponent10 = 0;

			// The maximum integral radix-based exponent representable by the type.
			static EA_CONSTEXPR_OR_CONST int max_exponent = 0;

			// The maximum integral base 10 exponent representable by the type.
			static EA_CONSTEXPR_OR_CONST int max_exponent10 = 0;

			// True if the type has a representation for positive infinity.
			static EA_CONSTEXPR_OR_CONST bool has_infinity = false;

			//  True if the type has a representation for a quiet (non-signaling) NaN.
			static EA_CONSTEXPR_OR_CONST bool has_quiet_NaN = false;

			// True if the type has a representation for a signaling NaN.
			static EA_CONSTEXPR_OR_CONST bool has_signaling_NaN = false;

			// An enumeration which identifies denormalization behavior.
			// In practice the application can change this at runtime via hardware-specific commands.
			static EA_CONSTEXPR_OR_CONST float_denorm_style has_denorm = denorm_absent;

			// True if the loss of accuracy is detected as a denormalization loss.
			// Typically false for integer types and true for floating point types.
			static EA_CONSTEXPR_OR_CONST bool has_denorm_loss = false;

			// True if the type has a bounded set of representable values. Typically true for 
			// all built-in numerial types (integer and floating point).
			static EA_CONSTEXPR_OR_CONST bool is_bounded = false;

			// True if the type has a modulo representation (if it's possible to add two
			// positive numbers and have a result that wraps around to a third number
			// that is less. Typically true for integers and false for floating types.
			static EA_CONSTEXPR_OR_CONST bool is_modulo = false;

			// True if trapping (arithmetic exception generation) is implemented for this type.
			// Typically true for integer types (div by zero) and false for floating point types,
			// though in practice the application may be able to change floating point to trap at runtime.
			static EA_CONSTEXPR_OR_CONST bool traps = false;

			// True if tiny-ness is detected before rounding.
			static EA_CONSTEXPR_OR_CONST bool tinyness_before = false;

			// An enumeration which identifies default rounding behavior.
			// In practice the application can change this at runtime via hardware-specific commands.
			static EA_CONSTEXPR_OR_CONST float_round_style round_style = round_toward_zero;

			// True if the type is floating point and follows the IEC 559 standard (IEEE 754).
			// In practice the application or OS can change this at runtime via hardware-specific commands or via compiler optimizations.
			static EA_CONSTEXPR_OR_CONST bool is_iec559 = false;
		};


		#if EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED
			extern EASTL_API float gFloatInfinity;
			extern EASTL_API float gFloatNaN;
			extern EASTL_API float gFloatSNaN;
			extern EASTL_API float gFloatDenorm;

			extern EASTL_API double gDoubleInfinity;
			extern EASTL_API double gDoubleNaN;
			extern EASTL_API double gDoubleSNaN;
			extern EASTL_API double gDoubleDenorm;

			extern EASTL_API long double gLongDoubleInfinity;
			extern EASTL_API long double gLongDoubleNaN;
			extern EASTL_API long double gLongDoubleSNaN;
			extern EASTL_API long double gLongDoubleDenorm;
		#endif

	} // namespace Internal

	using std::numeric_limits;

} // namespace eastl


EA_RESTORE_VC_WARNING()


#endif // Header include guard























