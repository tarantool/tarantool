#define DTABLE_OFFSET  32
#define SMALLOBJ_MASK  1
#define SHIFT_OFFSET   4
#define DATA_OFFSET    12
#define SLOT_OFFSET    16
.syntax unified
.fpu neon

// Macro for testing: logs a register value to standard error
.macro LOG reg
	push {r0-r3, ip,lr}
	mov r0, \reg
	bl  logInt(PLT)
	pop {r0-r3, ip,lr}
.endm

.macro MSGSEND receiver, sel
	.fnstart
	teq    \receiver, 0
	beq    4f                              // Skip everything if the receiver is nil
	push   {r4-r6}                         // We're going to use these three as
	.save  {r4-r6}
	                                       // scratch registers, so save them now.
	                                       // These are callee-save, so the unwind library
	                                       // must be able to restore them, so we need CFI
	                                       // directives for them, but not for any other pushes
	tst    \receiver, SMALLOBJ_MASK        // Sets Z if this is not a small int


	ldrne  r4, LSmallIntClass              // Small Int class -> r4 if this is a small int
	ldrne  r4, [r4]

	ldreq  r4, [\receiver]                 // Load class to r4 if not a small int

	ldr    r4, [r4, #DTABLE_OFFSET]        // Dtable -> r4

	ldr    r5, LUninstalledDtable          // &uninstalled_dtable -> r5
	ldr    r5, [r5]

	teq    r4, r5                          // If dtable == &uninstalled_dtable
	beq    5f                              // Do a slow lookup

	ldr    r5, [\sel]                      // selector->index -> r5

	ldr    r6, [r4, #SHIFT_OFFSET]        // dtable->shift -> r6
	ldr    r4, [r4, #DATA_OFFSET]         // dtable->data -> r4
	
	teq    r6, #8                         // If this is a small dtable, jump to the small dtable handlers
	beq    1f
	teq    r6, #0
	beq    2f

	and    r6, r5, #0xff0000
	ldr    r4, [r4, r6, asr#14]
	ldr    r4, [r4, #DTABLE_OFFSET]        
1:                                        // dtable16
	and    r6, r5, #0xff00
	ldr    r4, [r4, r6, asr#6]
	ldr    r4, [r4, #DTABLE_OFFSET]        
2:                                        // dtable8
	and    r6, r5, #0xff
	ldr    ip, [r4, r6, asl#2]

	teq    ip, #0                         // If the slot is nil
	beq    5f                             // Go to the slow path and do the forwarding stuff

	ldr   ip, [ip, #SLOT_OFFSET]          // Load the method from the slot

3: 
	pop    {r4-r6}                        // Restore the saved callee-save registers
	mov    pc, ip

4:                                        // Nil receiver
	mov    r0, 0
	mov    r1, 0
	mov    pc, lr
5:                                        // Slow lookup
	push   {r0-r4, lr}                    // Save anything that will be clobbered by the call
	.save  {r0-r4, lr}


	push   {\receiver}                    // &self, _cmd in arguments
	.save  {\receiver}
	mov    r1, \sel

	bl     slowMsgLookup(PLT)             // This is the only place where the CFI directives have to be accurate...
	mov    ip, r0                         // IMP -> ip

	pop    {r5}                           // restore (modified) self to r5
	pop    {r0-r4, lr}                    // Load clobbered registers
	mov    \receiver, r5
	b      3b
	.fnend
.endm

.globl objc_msgSend_fpret
	.type	objc_msgSend_fpret, %function
.globl objc_msgSend
	.type	objc_msgSend, %function
objc_msgSend:
objc_msgSend_fpret:
	MSGSEND r0, r1
.globl objc_msgSend_stret
	.type	objc_msgSend_stret, %function
objc_msgSend_stret:
	MSGSEND r1, r2

LSmallIntClass:
	.long   SmallObjectClasses
	.align  2
LUninstalledDtable:
	.long   uninstalled_dtable
	.align  2
