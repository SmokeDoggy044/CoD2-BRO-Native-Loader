#include "functions.h"

#include "ngl.h"
#include "script_string.h"
#include "tr_main.h"

namespace game {

namespace {

template <typename F>
const void* code(F* function) {
    return reinterpret_cast<const void*>(function);
}

// Sorted by campaign address.
const Function kFunctions[] = {
    {"nglInitQuad", 0x0005ddb0, 0, code(&nglInitQuad), 4, Result::None, Status::Compare},
    {"nglSetQuadTexture", 0x0005de20, 0, code(&nglSetQuadTexture), 8, Result::None, Status::Compare},
    {"nglSetQuadFlags", 0x0005de30, 0, code(&nglSetQuadFlags), 8, Result::None, Status::Compare},
    {"nglSetQuadBlend", 0x0005de40, 0, code(&nglSetQuadBlend), 12, Result::None, Status::Compare},
    {"nglSetQuadUV", 0x0005de60, 0, code(&nglSetQuadUV), 20, Result::None, Status::Compare},
    {"nglSetQuadColor", 0x0005deb0, 0, code(&nglSetQuadColor), 8, Result::None, Status::Compare},
    {"nglSetQuadRect", 0x0005ded0, 0, code(&nglSetQuadRect), 20, Result::None, Status::Compare},
    {"nglSetQuadZ", 0x0005df20, 0, code(&nglSetQuadZ), 8, Result::None, Status::Compare},
    {"nglSetQuadVertexPos", 0x0005df30, 0, code(&nglSetQuadVertexPos), 16, Result::None, Status::Compare},
    {"nglSetQuadVertexUV", 0x0005df60, 0, code(&nglSetQuadVertexUV), 16, Result::None, Status::Compare},
    {"nglSetQuadVertexColor", 0x0005df90, 0, code(&nglSetQuadVertexColor), 12, Result::None, Status::Compare},
    {"nglSetClearFlags", 0x0005e430, 0, code(&nglSetClearFlags), 4, Result::None, Status::Compare},
    {"nglSetClearDepth", 0x0005e450, 0, code(&nglSetClearDepth), 4, Result::None, Status::Compare},
    {"nglSetColorWriteMask", 0x0005e470, 0, code(&nglSetColorWriteMask), 4, Result::None, Status::Compare},
    {"nglSetFog", 0x0005e4d0, 0, code(&nglSetFog), 16, Result::None, Status::Compare},
    {"nglPopScene", 0x0005e730, 0, code(&nglPopScene), 0, Result::Eax, Status::Compare},
    {"nglSetCurrentScene", 0x0005e750, 0, code(&nglSetCurrentScene), 4, Result::Eax, Status::Compare},
    {"nglSetClearColor", 0x0005eaa0, 0, code(&nglSetClearColor), 16, Result::None, Status::Compare},
    {"nglSetFogColor", 0x0005eb60, 0, code(&nglSetFogColor), 12, Result::None, Status::Compare},
    {"nglMatrixPerspective", 0x0005ec20, 0, code(&nglMatrixPerspective), 20, Result::None, Status::Compare},
    {"nglMatrixOrtho", 0x0005ed60, 0, code(&nglMatrixOrtho), 20, Result::None, Status::Compare},
    {"nglMatrixScreenToClip", 0x0005f010, 0, code(&nglMatrixScreenToClip), 4, Result::None, Status::Compare},
    {"nglMatrixViewport", 0x0005f1b0, 0, code(&nglMatrixViewport), 20, Result::None, Status::Compare},
    {"nglGetScreenWidth", 0x00064640, 0, code(&nglGetScreenWidth), 0, Result::Eax, Status::Compare},
    {"nglGetScreenHeight", 0x00064650, 0, code(&nglGetScreenHeight), 0, Result::Eax, Status::Compare},
    {"nglColorToVec4", 0x0006e780, 0, code(&nglColorToVec4), 8, Result::None, Status::Compare},
    {"nglDepthToZValue", 0x0006e860, 0, code(&nglDepthToZValue), 4, Result::St0, Status::Compare},
    {"nglSetRenderTarget", 0x00073a70, 0, code(&nglSetRenderTarget), 4, Result::None, Status::Compare},
    {"nglSetSceneFlags", 0x00073ae0, 0, code(&nglSetSceneFlags), 4, Result::None, Status::Compare},
    {"nglSwapRedBlue", 0x0007b220, 0, code(&nglSwapRedBlue), 8, Result::None, Status::Compare},
    {"ScriptStringRep_Init", 0x0011d540, 0, code(&ScriptStringRep_Init), 12, Result::Eax, Status::Compare},
    {"ScriptStringRep_Append", 0x0011d580, 0, code(&ScriptStringRep_Append), 8, Result::None, Status::Compare},
    {"ScriptString_FromRep", 0x0011d5d0, 0, code(&ScriptString_FromRep), 4, Result::Eax, Status::Compare},
    {"ScriptString_Copy", 0x0011d5e0, 0, code(&ScriptString_Copy), 4, Result::Eax, Status::Compare},
    {"ScriptString_Find", 0x0011d6a0, 0, code(&ScriptString_Find), 8, Result::Eax, Status::Compare},
    {"ScriptString_FindChar", 0x0011d700, 0, code(&ScriptString_FindChar), 8, Result::Eax, Status::Compare},
    {"ScriptString_FindLast", 0x0011d730, 0, code(&ScriptString_FindLast), 4, Result::Eax, Status::Compare},
    {"ScriptString_SetChar", 0x0011d7a0, 0, code(&ScriptString_SetChar), 8, Result::None, Status::Compare},
    {"ScriptString_Equals", 0x0011d7c0, 0, code(&ScriptString_Equals), 8, Result::Al, Status::Compare},
    {"ScriptString_EqualsString", 0x0011d820, 0, code(&ScriptString_EqualsString), 8, Result::Al, Status::Compare},
    {"ScriptKeyTable_Find", 0x0011db70, 0, code(&ScriptKeyTable_Find), 4, Result::Eax, Status::Compare},
    {"RE_ClearScene", 0x00234cc0, 0, code(&RE_ClearScene), 0, Result::None, Status::Compare},
    {"R_ClearFrameLists", 0x00234cf0, 0, code(&R_ClearFrameLists), 0, Result::None, Status::Compare},
    {"myGLMultMatrix", 0x002352c0, 0, code(&myGLMultMatrix), 0, Result::None, Status::Compare},
    {"RE_SetFarCullDist", 0x00235dc0, 0, code(&RE_SetFarCullDist), 4, Result::None, Status::Compare},
    {"RE_GetFarCullDist", 0x00235e90, 0, code(&RE_GetFarCullDist), 0, Result::St0, Status::Compare},
};

}  // namespace

std::span<const Function> functions() { return kFunctions; }

}  // namespace game
