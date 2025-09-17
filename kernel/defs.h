#pragma once

#include "types.h"

// number of elements in fixed-size array (kept for users of NELEM)
#ifndef NELEM
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
#endif
