/*
HOST_BOOTSTRAP.S

TEMPORARY: phase 2 of port/ps5/PS5.md deletes this file with
host_bootstrap.c, whose import thunks lead here (the guest probe's
guest_probe.S, trimmed to this).

import_dispatch is where every entry of the guest's import table leads: each
entry is a thunk that loads its own index into %r10 (which no call passes
arguments in) and jumps here. The guest reached the thunk with a call, so
its arguments are still in their registers and on the stack as the host's
calling convention has them, and a jump to C passes all of them on.
*/

	.text
	.p2align 4
	.globl import_dispatch

/* the guest's thread is the only one that calls imports */
import_dispatch:
	movl %r10d, current_import(%rip)
	jmp import_called
