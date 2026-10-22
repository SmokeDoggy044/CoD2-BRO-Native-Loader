# NAL animation codec (0x0007bac0 - 0x00093000)

The engine's skeletal animation library is called NAL in the binary: the channel classes register themselves
under the names `NAL_RLE8Int1` ... `NAL_EntropyTrajectoryPositionOrientation` (strings 0x007c2dd4..0x007c2fb0).

## Registration and managers
- Class descriptors at 0x008bb130 (stride 0x14): `{prototype vtable, registrar vtable 0x00832774, next, name, &prototype}`.
  Static-init stubs after 0x00378830 push each registrar onto the list 0x00909840 (the clip type `generic`,
  registrar vtable 0x007c23ac, goes first). Clip data names a channel class by string; the prototype is cloned.
- `Anim_InitManagers` (Sys_Main) creates four skip-list managers:
  0x0090982c clips (Stream_LoadXbanim -> 0x0008dbb0, Stream_ReleaseAnims), 0x00909828 second clip table (key +0x10,
  released in Stream_ReleaseAnims), 0x00909830 clip lookup used by AnimData_EnsureLoaded / CG_RegisterWeapon (0x0008d950),
  0x00909838 skeletons (Stream_LoadXbskel -> 0x0008e130, DObj_UpdateSkeletons -> 0x0008e0d0). Block cache: 0x00903f9c.

## Clip, groups, state
- Clip class `generic`, vtable 0x007c23b4: 0 DeletingDtor, 1 Fixup after load (0x00087030), 3 version == 0x10302,
  4 group-range count (+0x60), 5 group range i (+0x64, 20-byte entries), 6 evaluate pose (0x000886f0, from DObj_UpdateSkeletons),
  9 embedded state (+0xc8), 10 new state (16 bytes, 0x00087ad0). Skeleton files use version 0x10301 (vtable 0x007c23ec).
- Channel groups: clip +0x84 count, +0x88 array of 0x30-byte groups; group +0x20 channel object (vtable below),
  +0x24 first channel index, +0x28 channel count. Active channels: bit set in clip +0x54.
- Every method gets a context `{clip, group, &stream A (+8), &stream B (+0xc)}` or (group, &dst, &src);
  every stream pointer is 4-byte aligned before use (2-byte for int16 data).

## Channel vtable (26 slots, 0x68 bytes)
| slot | role | driver |
|---|---|---|
| 0 | type id: address of a tag byte 0x00909820 (Float3) 21 (Int1) 22 (IKSpin) 23 (Float1) 24 (PosOri) 25 (Float4) 26 (Quat) | 0x00087dd0 (AnimTree_AllocInfo) |
| 1 / 24 | value size / component size (1, 4, 0xc, 0x10, 0x20, 0x50) | 0x00087dd0 |
| 2 | Blend (count, dst, a, b, weight) per group; group +0x2c = its offset in the state data | NAL_AnimState_Blend 0x00087890 |
| 3 | construct (0x0008b5e0.., outside this range) | 0x000879a0, 0x0008d9b0.. |
| 4 | Fixup: walk the clip data of all channels after loading | 0x00087030 |
| 5 | BeginBlock: per active channel set up a reader (plain pointer, RLE reader or 16-byte bit readers) | - |
| 6 | DecodeFrames: decode the next n frames from the readers | - |
| 7 | DecodeBlock: decode a key block into the cache entry | 0x000874b0 (block = time / clip +0x68) |
| 8 | SkipBlock: block already cached, only advance the pointers | 0x000874b0 |
| 9 | Evaluate: write value or default into the pose at the per-channel offset (< 0 = unused) | 0x00087690 |
| 10 / 11 | ApplyStatic / SkipStatic: per-channel constant data (ctx +8 / +0xc) | 0x000881c0 |
| 12 / 13 | InitState / FreeState (state = n * value size for plain types) | 0x00087ad0, 0x00087bb0 / 0x00086da0 |
| 14 | CopyState (dst, src) | 0x00086e50, 0x00086ec0, 0x00087690, 0x00087890 |
| 15 | SkipCachedFrame: one cached frame of each active channel | block-cache flush 0x000872e0 |
| 18-23 | small helpers (+4 skips; 22/23 only the Trajectory classes) | |

Plain classes (SignalCounter, Float3, Float4, Quaternion, PositionOrientation) store raw values; slot 10 is empty for most.

## Stream formats
- RLE8: reader `{ptr, u8 run, u8 value}`, pairs {count, value} (`NAL_RleReader_ReadBytes`).
- Entropy: per component a byte-length-prefixed bit stream; reader `{u32 ptr | byte offset in low 2 bits, u8 bit pos,
  s8 mode (0xff = read a 5-bit header), u16 zero run}`. `NAL_EntropyReader_DecodeFloats` (0x0007cb10) bit-unpacks values
  times a per-channel scale; `NAL_EntropyReader_Decode` (0x0007cea0) emits the zero run then dispatches on the mode
  through the table 0x008bb4a0 (0x0008f440..).
- Packed8 / Packed16: entropy-decoded values clamped to -127..127 / -32767..32767 and cached as int8 / int16;
  Evaluate dequantizes with value * range +8 + range +4 (8-byte {bias, scale} record per channel).
- Cached frame sizes: Float1 4, Float3 12, Packed8Float3 3, Packed16Float3 6, Float4 16 bytes per channel.

## Not done yet
Slots 2/3, the Quaternion / PositionOrientation / Trajectory / IKSpin classes (0x00081c80..0x00086850), the manager
methods (0x00083c60..0x00085aa0) and the driver functions 0x00086d30..0x000886f0 and 0x0008d950..0x0008ee80.
No caller of slots 5 and 6 through the vtable has been found yet.

## DObj_UpdateSkeletons and its callees (calling conventions)


Most of these set up an aligned frame (`push ebp; mov ebp,esp; and esp,-16`). Stack arguments are then read through EBP and locals through ESP.
Locals copied with `movaps` must stay 16-byte aligned (`__declspec(align(16))` locals, or `__m128`).
A "4x4" here is four 16-byte rows. Row 3 is the translation, and the matrices are row vectors (`v * M`).

| address | name | inputs | returns | watch |
|---|---|---|---|---|
| 0001c5b0 | Mat44_MultiplyInPlace | ECX = A (this), stack (out, B); callee pops 8 | EAX = out | Writes A*B back into A *and* out. SSE only. |
| 0001cae0 | QuatTrans_Concat | cdecl (out, a, b), 32-byte {quat, pos} | EAX = out | Calls Quat_ToMat33 (thiscall on a stack local) and Vec3_MulMat44. Constant 0x008375d0. |
| 0001cbe0 | QuatTrans_ToMat44 | EAX = out matrix, ECX = {quat, pos}; no stack args | none (EAX still out) | Register-only call. Sign-mask constant 0x0082e0b0; constants 0x0082e6a0/b0 = 0/1 vectors. |
| 0001db80 | Anim_CreateClipState | EAX = memory context id (-1 = none), cdecl (clip) | EAX = new 16-byte state or 0 | Saves/restores the 0x0095f5d0/d1 flags around MemContext_Push/Pop. When EAX is -1, the BL save comes from an uninitialized stack slot (harmless). |
| 000886f0 | NAL_GenericAnim_EvaluatePose | ECX = clip, stack (state, bones, range); callee pops 0xc | EAX = byte-code end pointer (unused) | 10 KB with a 0x8b4-byte frame. Byte-code interpreter: index table 0x0008af44, jump table 0x0008af24. 1245 SSE and 26 x87 instructions. Calls Quat_Slerp, Mat44_MultiplyInPlace, Quat_FromAxis3, Mat44_InverseRigid, NAL_IK_TwoBoneAngles, 0x00093250. |
| 0008e0d0 | NAL_FindSkeleton | cdecl (HashStr *name) | EAX = skeleton clip or 0 | Calls through the vtable: manager 0x00909838, slot 2. |
| 00242540 | XModel_TransformBones | cdecl (model, bones, parent) | nothing meaningful | BoneMatrix_Load/Store take EAX/ECX register arguments. Bone stride comes from the LOD. 68 SSE instructions. |
| 002b7ba0 | DObj_GetAnimRootTransform | cdecl (out, dobj) | nothing meaningful | Identity fill with `rep movsd` from an aligned stack local. Compares the vtable pointers with 0x00903fdc (the generic clip prototype's vtable) before calling. |
| 002beb60 | AnimTree_CalcPose_r | cdecl, 6 args (node, tree?, dest state, flag 1, 0, time/entity) | AL = root-motion flag (track header +0x34 bit 1) | Recursive. Reads the tree global 0x00928588 and the anim info array 0x00965d00 (0x30 per entry). Pushes/pops the memory context. Frees an old track through its vtable slot 0 (`call [edx]`, deleting dtor). |
| 000262a0 | Quat_FromAxis3 | cdecl (out, rows) | EAX = out | x87 fsqrt/fdivr spill through [esp+0xc]: decompile as float `sqrtf` and division. 4 branches. |
| 00086ae0 | Mat44_InverseRigid | ECX = matrix, stack (out); callee pops 4 | EAX = out | Scalar SSE only. |
| 000930b0 | NAL_IK_TwoBoneAngles | cdecl, 13 stack words (points, lengths, 4 output float pointers) | none | Mixes x87 (fld/fmul/fsqrt/fstp) with SSE `comiss` clamps. No aligned frame. |
| 0001c890 | Quat_ToMat33 | ECX = out rows, stack (quat); callee pops 4 | EAX = ECX | Aligned frame. Writes 3 rows. |
| 0001c9f0 | Vec3_MulMat44 | cdecl (out, vec, matrix) | EAX = out | `movups` (vec may be unaligned) and `movaps` (matrix rows). |
| 00086d30 | NAL_GenericAnim_GetRootTransform | ECX = clip, stack (state, out); callee pops 8 | none | `rep movsd` 8 dwords. Default record 0x00909850. |
| 00087ad0 | NAL_AnimState_Init | ECX = state, stack (clip, param); callee pops 8 | EAX = state | Scratch arena 0x00904010..0x00906810 (top 0x00906814; the previous top is stored in front of the block). Calls the channel through its vtable: slot 12 (`[edx+0x30]`). |
| 00086da0 | NAL_AnimState_Destroy | ECX = state | none | Calls the channel through its vtable: slot 13 (`[edx+0x34]`). Pops the arena by reading the saved top at data-4 (0x0090400c + top). |
| 00086e50 | NAL_AnimState_Copy | ECX = dst, stack (src); callee pops 4 | none | Calls the channel through its vtable: slot 14 (`[edx+0x38]`). Pushes the pointers to stack locals as the stream cursors. |
| 00087890 | NAL_AnimState_Blend | cdecl (dst, a, b, weight float) | none | Calls the channel through its vtable: slot 2 (`[edx+0x8]`), 5 args (count, dst, a, b, weight). `cmovle`/`cmovl` pick the range. |
| 000881c0 | NAL_AnimTrack_Sample | ECX = track, 6 stack args; callee pops 0x18 | unused | SSE `cvttss2si`/`cvtsi2ss` fraction wrapping. Calls channel slots 10/11 (`[edx+0x28]`/`[edx+0x2c]`) and 0x00087810. Temporary states live on the stack (scratch-arena path). |

Correction to docs/nal_animation.md: channel slot 2 is **Blend(count, dst, a, b, weight)** (NAL_AnimState_Blend). It is not clone/construct.
0x00093250 (12 stack args; builds three matrices with the row-copy helper 0x000260d0 (`ECX` = out, 4 row pointers, callee pops 0x10); called by two IK opcodes of the evaluator) is left unnamed.

## What DObj_UpdateSkeletons does
`DObj_UpdateSkeletons(dobj)` (cdecl, one argument) returns early in these cases: there is no bone buffer (DObj +0x20), the owning entity (+0x88) has flags 0x80400000 or +0x3b0 set, or there are no models (+0x2e).
Otherwise it walks the models at +0x38. For each model it takes the first LOD's skeleton clip (+0x38 of the LOD record). If that is missing, or is not a `generic` NAL clip (vtable compared with 0x00903fdc), it looks up the shared skeleton named `simple` (HashStr of the string at 0x00832a28, NAL_FindSkeleton) and stores it on the model.
It creates the DObj's clip state at +0x68 once (Anim_CreateClipState, memory context +0x34). When the DObj has an anim tree (+0), it puts the tree in 0x00928588 and runs AnimTree_CalcPose_r into the clip's embedded state (+0xc8); this remembers whether root motion was produced.
If the model has at least as many bones as the clip range, NAL_GenericAnim_EvaluatePose writes the pose into the DObj bone buffer at slot +0x60[i] (64-byte matrices from +0x30). Models attached to a parent bone (+0x58[i] != 0xff) are then moved under it with XModel_TransformBones. Models without a clip get XModel_CalcSkeleton(model, bones, parent bone) instead.
For an entity with flag 0x1000000, the root transform (DObj_GetAnimRootTransform, QuatTrans_ToMat44) is applied in one of three ways:
- **Tag-linked entity** (+0x3cc): the root becomes the link's local matrix. This is combined with G_GetTagParentMatrix through MatrixMultiply43, and the angles come from AxisToAngles.
- **Accumulated record** (+0x3d4): the root delta is concatenated into it with QuatTrans_Concat, then the stored quaternion is masked/renormalized with the vector constants 0x0082e660/0x0082e670 (exact meaning not checked).
- **Otherwise:** the delta is applied to the entity's current axis (AnglesToAxisLeft of +0x150, Mat44_MultiplyInPlace).

The resulting origin and angles go to G_SetOrigin2 and G_SetAngle2.
