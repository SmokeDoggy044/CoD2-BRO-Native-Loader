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
| 2, 3 | clone / construct (0x0008b5e0.., outside this range) | 0x00087890, 0x000879a0, 0x0008d9b0.. |
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
