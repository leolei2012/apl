/** Private composition helpers; not a host API or a stable buffer interface. */
#ifndef APL_INTERNAL_H
#define APL_INTERNAL_H

#include "apl_hmi.h"

struct apl_core;
#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
const struct apl_keymap *apl_core_keymap_target(const struct apl_core *self) APL_CALL;
#endif

/* Valid only during the foreground root tick; never retain or expose this pointer.
 * Fill the entire target, then commit before HMI processing. No callbacks occur
 * between obtaining the target and committing it. Each HMI owns its own target. */
struct apl_link_snapshot *apl_hmi_snapshot_target(struct apl_hmi *self) APL_CALL;
void apl_hmi_snapshot_commit(struct apl_hmi *self) APL_CALL;

#endif
