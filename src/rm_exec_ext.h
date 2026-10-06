// rm_exec_ext.h -- executors of the extended RM commands (docs/rm-gui/extended-commands-concept.md 3).
//
// rm_runtime.cpp execute() asks both after its own table. Firmware-only glue: the gathering of node
// state lives here, every reply text is built by the pure formatters of rm_format.h and every
// free-text / position argument is checked by rm_text.h, so the logic stays host-tested.
//
// Return: 0 = not my command, 1 = done ("ok ..." in res), -1 = failed ("err <token>" in res).
// res holds n bytes (RM_MAX_RESULT + 1); a result never exceeds RM_MAX_RESULT characters.
// Both run on the loop task, from rmDrain().
#ifndef RM_EXEC_EXT_H
#define RM_EXEC_EXT_H

#include <stddef.h>

#include "remote_cmd.h"

// reads: radio, name, atxt, pos (no args), sens, mh, txq, mbox, maxhop
int rmExecRead(const RmCmd &c, char *res, size_t n);
// writes: name <text>, atxt <text>, pos <lat> <lon> <alt>
int rmExecWrite(const RmCmd &c, char *res, size_t n);

#endif // RM_EXEC_EXT_H
