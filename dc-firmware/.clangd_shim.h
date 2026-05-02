// clangd-only shim: pre-include stdlib.h with random/srandom renamed
// to prevent ovl_diff_return_type conflict with Teensy WProgram.h
#define random __stdlib_random
#define srandom __stdlib_srandom
#include <stdlib.h>
#undef random
#undef srandom
