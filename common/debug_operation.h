#ifndef DEBUG_OPERATION_H_
#define DEBUG_OPERATION_H_

#include <stdio.h>
#include <assert.h>

//#define DEBUG 1

#define error(args ...) fprintf(stderr, args); exit(1)
#define warn(args ...) if (DEBUG) fprintf(stderr, args)
#define debug(args ...) if (DEBUG) printf(args)
#define debug_v2(args ...) if (DEBUG >= 2) printf(args)
// For printing HTTP request and storage request body (WARNING: might contain long file content)
#define debug_v3(args ...) if (DEBUG >= 3) printf(args)

#endif
