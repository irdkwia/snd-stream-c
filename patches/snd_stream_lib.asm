; 
; @file snd_stream_lib.asm
; @author Irdkwia & Adakite
; @brief SND Stream Library C Edition
; @details Port of SND Stream Library to C
; @version 0.8.6
; @date 2026-02-22

.nds
.include "symbols.asm"

.definelabel RESERVE_CHANNEL, 14

.open "arm9.bin", arm9_start
	.org EndHookStartBGM-0x4
	.area 0x4
		b HookStartBGM ; -> Hook Play BGM: r6: ID, r5: Fade In, r4: Volume
	.endarea

	.org EndHookStartBGM2-0x4
	.area 0x4
		b EndOfStartBGM
	.endarea

	.org EndHookStopBGM-0x4
	.area 0x4
		b HookStopBGM ; -> Hook Stop BGM: r0/r4: Fade Out
	.endarea

	.org EndHookChangeBGM-0x4
	.area 0x4
		b HookChangeBGM ; -> Hook Change BGM: r5: Duration, r4: Volume
	.endarea

	.org EndHookSoundProcess-0x4
	.area 0x4
		b hook_check_overlay_arm9
	.endarea
	
	.org HookChannel1
	.area 0x4
		mov r2,RESERVE_CHANNEL
	.endarea
	.org HookChannel2
	.area 0x4
		cmp r0,RESERVE_CHANNEL
	.endarea

	.org HookCheckOverlayArm9
	.area 0x10
		.word EndHookSoundProcess
hook_check_overlay_arm9:
		bl OS_SleepThread ; Original instruction.
		ldr r0,[HookCheckOverlayArm9]
		bx r0
	.endarea
.close