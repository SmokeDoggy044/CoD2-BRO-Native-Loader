#pragma once

// Our own versions of XAPILIB's save-game functions (XCreateSaveGame & co., the save signatures) and their closure,
// for the own engine host. Like title_xapi.h: the same kernel exports through the title's thunk table, the same
// results, last errors and files. A save game is a directory "<root><12 hex digits of the name's hash>\" holding
// SaveMeta.xbx (UTF-16 with a byte order mark: "Name=<name>\r\n", "NoCopy=1\r\n" for no-copy saves).

#include <cstdint>
#include <string>

#include "compiler.h"

namespace game {

// --- Signatures (XCALCSIG: +0 type, +4 HMAC key, +8 SHA context) ---
void XB_STDCALL XShaHmacInitialize(const uint8_t* key, uint32_t key_length, uint8_t* sha);                  // 0x001e87bb
void XB_STDCALL XShaHmacComputeFinal(uint8_t* sha, const uint8_t* key, uint32_t key_length, uint8_t* digest);  // 0x001e8822
uint32_t XB_STDCALL XapiConvertSignatureToNonRoamable(uint8_t* context, const uint8_t* signature,
                                                      uint8_t* out);                                       // 0x001e8894
uint32_t XB_STDCALL XapipCalculateSaveGameSignatureBegin(uint32_t flags, uint32_t title_id);                // 0x001e8909
uint32_t XB_STDCALL XapipCalculateSaveGameSignatureEnd(uint32_t context, uint8_t* signature);               // 0x001e8989
uint32_t XB_STDCALL XapipCalculateContentSignatureBegin();                                                  // 0x001e89eb
uint32_t XB_STDCALL FUN_001e8a21(uint32_t context, const uint8_t* data, uint32_t size);  // SHA update of a context
uint32_t XB_STDCALL XapipCalculateContentSignatureEnd(uint32_t context, uint8_t* signature);                // 0x001e8a3b
uint32_t XB_STDCALL XCalculateSignatureBeginEx(uint32_t flags, uint32_t title_id);                          // 0x001e8a86
uint32_t XB_STDCALL XCalculateSignatureBegin(uint32_t flags);                                               // 0x001e8ac7
uint32_t XB_STDCALL XCalculateSignatureUpdate(uint32_t context, const uint8_t* data, uint32_t size);        // 0x001e8adb
uint32_t XB_STDCALL XCalculateSignatureEnd(uint32_t context, uint8_t* signature);                           // 0x001e8b29

// --- Save games ---
uint32_t XB_STDCALL HexDigitToChar(int32_t digit);                                                         // 0x001e8b6e
void XB_STDCALL XapiSaveGameHashEncode(const uint16_t* name, char* out, uint32_t size);  // 0x001e8b82 (size unused)
uint32_t XB_STDCALL XapiValidateAndSkipUnicodeSignature(uint32_t file, uint32_t rewind);                   // 0x001e8c14
uint32_t XB_STDCALL XapiFindValueInMetaFile(uint32_t file, const uint16_t* key, uint16_t* value,
                                            int32_t count);                                                // 0x001e8c5c
uint32_t XB_STDCALL XapiFillInSaveGameData(uint8_t* find, const char* root, uint32_t root_length);         // 0x001e8ddf
uint32_t XB_STDCALL XapiVerifyGameName(const char* meta_path, const uint16_t* name);                       // 0x001e8ee0
void XB_STDCALL XapiTouchDirectoryTimestamp(const char* path);                                             // 0x001e8f60
uint32_t XB_STDCALL XCreateSaveGame(const char* root, const uint16_t* name, uint32_t disposition, uint32_t flags,
                                    char* path, uint32_t path_size);                                       // 0x001e8fbc
uint32_t XB_STDCALL XDeleteSaveGame(const char* root, const uint16_t* name);                               // 0x001e921f
uint32_t XB_STDCALL XFindFirstSaveGame(const char* root, uint8_t* find);                                   // 0x001e92ef
uint32_t XB_STDCALL XFindNextSaveGame(uint32_t search, uint8_t* find);                                     // 0x001e93f9
uint32_t XB_STDCALL XFindClose(uint32_t search);                                                           // 0x001e9440
uint32_t XB_STDCALL XapiGetDirectoryDisplayBlocks(uint32_t directory, uint32_t* blocks, uint8_t* buffer);  // 0x001e9f12
uint32_t XB_STDCALL XGetDisplayBlocks(const char* path);                                                   // 0x001ea047

// --- Strings, directories ---
int32_t XB_STDCALL UnicodeToUTF8(const uint16_t* source, int32_t length, char* out, int32_t out_size);     // 0x001e95ec
int32_t XB_STDCALL WideCharToMultiByte(uint32_t code_page, uint32_t flags, const uint16_t* source, int32_t length,
                                       char* out, int32_t out_size, const char* default_char,
                                       uint32_t* used_default);                                            // 0x001e975f
char* XB_STDCALL lstrcpynA(char* dest, const char* source, uint32_t count);                                // 0x001eabd6
uint32_t XB_STDCALL CreateDirectoryA(const char* path, uint32_t security);                                 // 0x001ed5d0
uint32_t XB_STDCALL RemoveDirectoryA(const char* path);                                                    // 0x001ed63c
uint32_t XB_STDCALL XapiNukeDirectoryFromHandle(uint32_t directory, uint8_t* buffer);                      // 0x001edfe6
uint32_t XB_STDCALL XapiNukeDirectory(const char* path, uint32_t root);                                    // 0x001ee163

// --check-crt: the functions above against the title's (title_savegame_check.cpp); the number of differences.
int32_t check_title_savegame(std::string& report);

}  // namespace game
