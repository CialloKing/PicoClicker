#include <stddef.h>
#include <windows.h>

extern UINT_PTR __security_cookie;
#ifdef _M_IX86
extern PVOID __safe_se_handler_table[];
extern BYTE __safe_se_handler_count;
#else
extern volatile PVOID __guard_dispatch_icall_fptr;
#endif
extern volatile PVOID __guard_check_icall_fptr;
extern PVOID __guard_fids_table[];
extern ULONG __guard_fids_count;
extern ULONG __guard_flags;
#ifndef PICOCLICKER_MIN_LOAD_CONFIG
extern PVOID __guard_iat_table[];
extern ULONG __guard_iat_count;
extern PVOID __guard_longjmp_table[];
extern ULONG __guard_longjmp_count;
#endif

// Use the PE load configuration prefix through the last CFG table required by LLD.
typedef struct
{
    DWORD size;
    BYTE reserved[offsetof(IMAGE_LOAD_CONFIG_DIRECTORY, SecurityCookie) - sizeof(DWORD)];
    UINT_PTR securityCookie;
    UINT_PTR handlerTable;
    UINT_PTR handlerCount;
    UINT_PTR checkFunction;
    UINT_PTR dispatchFunction;
    UINT_PTR functionTable;
    UINT_PTR functionCount;
    // Clang needs a pointer-sized initializer for LLD's absolute 32-bit flags.
    // On x64 the high DWORD is zero and covers the start of the unused CodeIntegrity field.
    UINT_PTR guardFlags;
#ifndef PICOCLICKER_MIN_LOAD_CONFIG
    BYTE codeIntegrityTail[sizeof(IMAGE_LOAD_CONFIG_CODE_INTEGRITY) + sizeof(DWORD) - sizeof(UINT_PTR)];
    UINT_PTR addressTakenIatTable;
    UINT_PTR addressTakenIatCount;
    UINT_PTR longJumpTable;
    UINT_PTR longJumpCount;
#endif
} LoadConfig;

_Static_assert(offsetof(LoadConfig, guardFlags) == offsetof(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags),
    "PE guard flags layout");
#ifdef PICOCLICKER_MIN_LOAD_CONFIG
_Static_assert(sizeof(LoadConfig) == offsetof(IMAGE_LOAD_CONFIG_DIRECTORY, GuardFlags) + sizeof(UINT_PTR),
    "PE experimental load configuration prefix");
#else
_Static_assert(offsetof(LoadConfig, addressTakenIatTable) ==
    offsetof(IMAGE_LOAD_CONFIG_DIRECTORY, GuardAddressTakenIatEntryTable), "PE address-taken IAT table layout");
_Static_assert(sizeof(LoadConfig) == offsetof(IMAGE_LOAD_CONFIG_DIRECTORY, DynamicValueRelocTable),
    "PE load configuration prefix");
#endif

// The linker provides table entries and encodes the counts and flags as absolute symbols.
const LoadConfig _load_config_used =
{
    .size = sizeof(LoadConfig),
    .securityCookie = (UINT_PTR)&__security_cookie,
#ifdef _M_IX86
    .handlerTable = (UINT_PTR)__safe_se_handler_table,
    .handlerCount = (UINT_PTR)&__safe_se_handler_count,
#else
    .dispatchFunction = (UINT_PTR)&__guard_dispatch_icall_fptr,
#endif
    .checkFunction = (UINT_PTR)&__guard_check_icall_fptr,
    .functionTable = (UINT_PTR)__guard_fids_table,
    .functionCount = (UINT_PTR)&__guard_fids_count,
    .guardFlags = (UINT_PTR)&__guard_flags,
#ifndef PICOCLICKER_MIN_LOAD_CONFIG
    .addressTakenIatTable = (UINT_PTR)__guard_iat_table,
    .addressTakenIatCount = (UINT_PTR)&__guard_iat_count,
    .longJumpTable = (UINT_PTR)__guard_longjmp_table,
    .longJumpCount = (UINT_PTR)&__guard_longjmp_count
#endif
};
