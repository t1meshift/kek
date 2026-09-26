#ifndef KEK_TEST_SUPPORT_H
#define KEK_TEST_SUPPORT_H

/* Shared by the suites. Everything here goes through the engine's public API:
   the pool is measured by what it will hand out, not by reading its slots, so
   these helpers keep working when Tier 2 puts an arena underneath. */

#include "kek.h"

/* How many more models (textures) the pool will hand out right now. Takes
   them all, counts, and gives them back, so the pool is left as it was apart
   from the generations of the slots it touched. */
int kek_test_free_models(KEK_engine* e);
int kek_test_free_textures(KEK_engine* e);

#endif // KEK_TEST_SUPPORT_H
