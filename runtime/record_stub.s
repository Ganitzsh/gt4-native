# The return path of a call that runtime/record.cpp records: the recorded function returns here instead
# of to its caller. Saves the registers and the x87 state, has gt4_record_after write the state at return
# and give back the caller's address, restores everything and returns there.
	.text
	.globl	_gt4_record_return
_gt4_record_return:
	subl	$4, %esp		# room for the caller's return address
	pushal				# eax at 28(%esp), edx at 20(%esp)
	subl	$108, %esp
	fnsave	(%esp)
	leal	108(%esp), %eax
	pushl	%eax			# the pushal frame
	call	_gt4_record_after
	addl	$4, %esp
	movl	%eax, 140(%esp)		# 108 + 32: the slot above the pushal frame
	frstor	(%esp)
	addl	$108, %esp
	popal
	ret
