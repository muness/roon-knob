// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <cmath>
// M5Unified 0.2.28 calls isfinite without std:: qualification in Arduino mode.
// GCC 15/Picolibc exposes the C++ overloads only in std. Make that overload set
// available to the managed library without modifying its immutable source.
using std::isfinite;
