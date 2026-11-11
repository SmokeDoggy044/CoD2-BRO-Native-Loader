#pragma once

// Our own MSVC x86 C++ exception handling (VC7.1's model, which the title's code and data were compiled for), for
// the own engine host (0.3: no original .text). The title's functions keep their EH registration nodes on the stack
// (FS:[0] chain, {next, handler, state}, the frame's EBP 0xC above, its ESP saved 4 below) and their FuncInfo /
// unwind map / try block map / catchable type tables in .rdata; each handler stub is `mov eax, FuncInfo; jmp
// ___CxxFrameHandler`. A throw is RaiseException(0xE06D7363, noncontinuable, {0x19930520, object, ThrowInfo}); the
// loader's dispatch (src/runtime/loader/seh.cpp) calls the chain's handlers, and a catch never returns to it: the
// catch funclet runs, then execution jumps to the continuation it returns, on the catching frame's stack.
//
// Same behaviour as the title's LIBCMT where the game can see it: the order destructors run (unwind map, innermost
// state first), which catch takes the exception (try blocks by state, nested catch depth, type match by
// TypeDescriptor or name, const/volatile/reference rules), the catch object copy (simple types, copy constructors,
// virtual bases), rethrow (`throw;`) leaving a catch block, the exception object destroyed once by the last catch.
// The per-thread fields (current exception and context, the throw-in-progress count, the frame info list) live in
// the CRT's per-thread data (title_crt.h). Not ported: _set_se_translator (the title never sets one; foreign
// exceptions reach only catch(...)), FuncInfo versions after 0x19930520 (none in the title), the security cookie
// check of the catch guard.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001f2302 _CxxThrowException (stdcall): raises the C++ exception; `object` = 0 and `throw_info` = 0 rethrow.
void XB_STDCALL title_CxxThrowException(void* object, uint32_t throw_info);

}  // namespace game

// 0x001f1ec0 ___CxxFrameHandler: the cdecl exception handler with the function's FuncInfo in EAX.
extern "C" void title_CxxFrameHandler_entry(void);
// 0x001f2930 __EH_prolog: links the caller's registration node (handler stub in EAX) and sets up its EBP frame.
extern "C" void title_EH_prolog_entry(void);
