#include <stddef.h>
#include <windows.h>

extern UINT_PTR __security_cookie;
extern PVOID __safe_se_handler_table[];
extern BYTE __safe_se_handler_count;
extern volatile PVOID __guard_check_icall_fptr;
extern PVOID __guard_fids_table[];
extern ULONG __guard_fids_count;
extern ULONG __guard_flags;
extern PVOID __guard_iat_table[];
extern ULONG __guard_iat_count;
extern PVOID __guard_longjmp_table[];
extern ULONG __guard_longjmp_count;

// Use the PE32 prefix through the last CFG table required by the linker.
typedef struct
{
    DWORD size;
    BYTE reserved[offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, SecurityCookie) - sizeof(DWORD)];
    DWORD securityCookie;
    DWORD handlerTable;
    DWORD handlerCount;
    DWORD checkFunction;
    DWORD dispatchFunction;
    DWORD functionTable;
    DWORD functionCount;
    DWORD guardFlags;
    IMAGE_LOAD_CONFIG_CODE_INTEGRITY codeIntegrity;
    DWORD addressTakenIatTable;
    DWORD addressTakenIatCount;
    DWORD longJumpTable;
    DWORD longJumpCount;
} LoadConfig32;

_Static_assert(offsetof(LoadConfig32, guardFlags) == offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, GuardFlags),
    "PE32 guard flags layout");
_Static_assert(sizeof(LoadConfig32) == offsetof(IMAGE_LOAD_CONFIG_DIRECTORY32, DynamicValueRelocTable),
    "PE32 load configuration prefix");

// The linker provides table entries and encodes the counts and flags as absolute symbols.
const LoadConfig32 _load_config_used =
{
    .size = sizeof(LoadConfig32),
    .securityCookie = (DWORD)&__security_cookie,
    .handlerTable = (DWORD)__safe_se_handler_table,
    .handlerCount = (DWORD)&__safe_se_handler_count,
    .checkFunction = (DWORD)&__guard_check_icall_fptr,
    .functionTable = (DWORD)__guard_fids_table,
    .functionCount = (DWORD)&__guard_fids_count,
    .guardFlags = (DWORD)&__guard_flags,
    .addressTakenIatTable = (DWORD)__guard_iat_table,
    .addressTakenIatCount = (DWORD)&__guard_iat_count,
    .longJumpTable = (DWORD)__guard_longjmp_table,
    .longJumpCount = (DWORD)&__guard_longjmp_count
};
