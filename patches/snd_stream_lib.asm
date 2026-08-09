; 
; @file snd_stream_lib.asm
; @author Irdkwia & Adakite
; @brief SND Stream Library C Edition
; @details Port of SND Stream Library to C
; @version 0.9.0
; @date 2026-08-09

.nds
.include "symbols.asm"

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

	.org EndHookStart2BGM-0x4
	.area 0x4
		b HookStart2BGM ; -> Hook Play BGM: r6: ID, r5: Fade In, r4: Volume
	.endarea

	.org EndHookStart2BGM2-0x4
	.area 0x4
		b EndOfStart2BGM
	.endarea

	.org EndHookStop2BGM-0x4
	.area 0x4
		b HookStop2BGM ; -> Hook Stop BGM: r0/r4: Fade Out
	.endarea

	.org EndHookChange2BGM-0x4
	.area 0x4
		b HookChange2BGM ; -> Hook Change BGM: r5: Duration, r4: Volume
	.endarea

	.org EndHookDSEVoiceAllocate-0x4
	.area 0x4
		b HookDSEVoiceAllocate
	.endarea

	.org EndHookDSEVoiceAllocate2-0x4
	.area 0x4
		b HookDSEVoiceAllocate2
	.endarea

	.org EndHookSoundProcess-0x4
	.area 0x4
		b hook_check_overlay_arm9
	.endarea
	
	.org HookChannel1
	.area 0x4
		mov r2,#0x10
	.endarea
	.org HookChannel2
	.area 0x4
		cmp r0,#0x10
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