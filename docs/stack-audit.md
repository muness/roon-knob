# Firmware stack audit

Every firmware build runs `scripts/audit_firmware_stack.py` on its compiled Xtensa ELF and includes `stack-audit.json` in its artifact. The report lists all symbol frames it can identify and direct-call estimates for handler, task, thread and command entry points. It includes SDK code rather than silently excluding library costs.

This is a static diagnostic, not a proof of task-stack safety. Indirect calls, missing symbols, unrecognized frames, recursion, depth limits and analysis-visit limits are explicit gaps. Interrupt costs and dynamic stack adjustments are not modeled. Task allocations must be compared with complete runtime high-water measurements before claiming sufficient margin. The reports do not authorize automated destructive HTTP-route or playback testing on live devices.

Dial gates the settings handler, provisioning handler and command dispatcher at 2,048 bytes per frame. The original compiled settings frame failed at 6,848 bytes; after moving request scratch storage to heap it is 304 bytes. Provisioning fell from 6,000 to 656 bytes. Avoiding a 64-zone inventory copy reduced command dispatch from 9,056 to 1,440 bytes. The HTTP task remains 8 KiB; a small handler frame leaves room for nested formatting, logging and server calls.

Other findings are reported for review rather than assigned arbitrary per-task budgets. In the local Dial build, the next largest frames are the bridge worker (2,816 bytes) and storage write (2,368 bytes). These are not by themselves evidence of overflow. Hardware HTTP stress and sustained controller input tests remain required to validate the corrected artifact.
