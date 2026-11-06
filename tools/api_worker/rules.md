# Decompiling one Call of Duty 2: Big Red One (Xbox) level function into C++

You are part of an open-source decompilation of the original Xbox game. A native loader maps the original XBE
and runs its x86 code; decompiled functions replace original ones one at a time. Each request gives you one
function from a level's section (compiled game scripts: the script compiler turned each level's scripts into
C++, built with Visual C++ 7.1 and LTCG). The same rules apply to the Claude Code session agents working on
other rows; your answer is checked the same way theirs is.

## What your C must be
- **Behaviour-matching, not byte-matching.** Same calls in the same order with the same arguments, same stores
  to memory, same result, same calling convention, stack bytes and result register. It is checked by compiling
  it with the title's compiler and comparing the ordered list of title calls and image constants with the XBE
  (vc71match --facts), and safe rows also run side by side with the original in the game (compare harness).
- Portable C++20 (the code later builds on Linux and Windows; 32-bit x86 for now). No compiler extensions
  outside the XB_* macros. Code style: like the example files (title_global / title_function / slot / pointer,
  comments sparse and factual, hashes as hex literals with the field or string in a comment).
- The EH/SEH frame (`push -1; push handler; mov eax,fs:0 ...` and the state stores) is left out; every
  ScriptString / DynArray release on the normal path stays, in the original's order.
- Float expressions: the original's grouping and order exactly (`(t*t)*((float)-g*0.5f)` is not `t*t*-g*0.5f`).
- A cdecl parameter is never modified in place (the debug build writes the argument slot, which is compared):
  copy it to a local. Where the original itself stores into its own argument slot and a caller could observe
  it, keep that store with a volatile write and say so in notes.

## Traps found in the pilot (each one compiled cleanly and was wrong)
- 12-byte vector copies (`mov r,[eax]; mov [esp+X],r; mov r,[eax+4]; ...`) collapsed to one word by Ghidra:
  copy all three words.
- Arguments: Ghidra guesses the prototypes of level-section callees. Count every call's arguments from the
  pushes (and `sub esp,N` for by-value structs) and the `add esp,N` after it / the callee's `ret N`. A
  by-value ScriptString argument is `push ecx; mov eax,esp; mov [eax],ecx` plus a ScriptString_Copy into it.
- Chained thiscalls: ECX of the second call is the first call's EAX result.
- Hidden result: `mov eax,[esp+4]` (the out argument) before every ret means the function returns `out`.
- Code addresses (thread bodies, vtables, callbacks, functor invoke functions) are literal constants, not
  reads of a global.
- Signed vs unsigned compares and widths exactly as the instructions (jl/jb, movsx/movzx).

## Rows
- **safe rows** cannot reach a script thread switch; they are registered Status::Compare (your stack_bytes and
  result go into that entry) and tested in game.
- **switch rows** can reach a script thread switch (a wait; script threads save and restore the x86 stack);
  their C is written now, not registered, and checked statically only until script threads become coroutines.
  Write them exactly like safe rows.

## Calling other code
- A callee that has C (its declaration is listed) is called directly by name.
- Anything else: `title_function<RET XB_CONV(ARGS)>(0xADDRESS)(args)  // Name`; interface slots (the
  0x00922xxx block): `slot<RET XB_CDECL(ARGS)>(0x00922xxx)(args)  // Builtin`; globals:
  `title_global<T>(0xADDRESS)`; strings: `pointer<const char>(0xADDRESS)  // "text"`.
- x87 results: `long double` return types where the callee returns in ST0 (as our prototypes do).
- Register arguments other than thiscall's ECX: say so in notes and do your best with an XB_REGS* convention;
  a coordinator will check it.

## Answer
One JSON object (the schema is enforced): the C name (keep the given name; a FUN_xxxxxxxx row gets a neutral
descriptive name `BroLvl_<level>_<What>` - never names from later Call of Duty titles), the complete
definition(s), the one-line header declaration, stack_bytes, result kind, and notes.

## Checks you will see
After your answer the code is compiled and checked; you then get the compile errors or the vc71match --facts
differences and answer again. Known noise in --facts (not real differences): inline ScrField_* accessors and
BroScr_ArrayElement (ours inline, the title's out-of-line), a function address loaded once into a register
and called twice, the title's duplicated branch tails listing a call twice, a helper taking a function
address listing it before the calls that build its arguments. If only noise is left, return the same c_code
and explain each difference in noise_explanations. Never change correct code just to make a check quiet.
