#include <windows.h>
#include <stdio.h>
#include <psapi.h>
#include <vector>

using namespace std;

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define PANIC_STRING(reason) reason " at " __FILE__ ":" TOSTRING(__LINE__)
#define PANIC(reason) return panic(PANIC_STRING(reason));
#define CHECK_MEMORY(x) { if ((x) == NULL) { PANIC("Out of memory") } }
#define CHECK_SUCCESS(x) { if ((x) != 0) { PANIC("Failed") } }
#define CAL_NT_FUNC(x, y) \
{ \
  y = (x) GET_NT_FUNCTION(#y); \
  if (!y) { \
    PANIC("Could not load NT function " #y) \
  } \
}

#define NT_SUCCESS(x) ((x) >= 0)
#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004

#define SystemHandleInformation 16
#define ObjectBasicInformation 0
#define ObjectNameInformation 1
#define ObjectTypeInformation 2
#define INDENT " "
#define InitializeObjectAttributes(p, n, a, r, s) { \
    (p)->Length = sizeof(OBJECT_ATTRIBUTES); \
    (p)->RootDirectory = r; \
    (p)->Attributes = a; \
    (p)->ObjectName = n; \
    (p)->SecurityDescriptor = s; \
    (p)->SecurityQualityOfService = NULL; \
    }
#define OBJ_CASE_INSENSITIVE 0x00000040
#define SYMBOLIC_LINK_QUERY 0x0001
#define HEAP_CLASS_0 0x00000000 // Process heap
#define HEAP_CLASS_1 0x00001000 // Private heap
#define HEAP_CLASS_2 0x00002000 // Kernel heap
#define HEAP_CLASS_3 0x00003000 // GDI heap
#define HEAP_CLASS_4 0x00004000 // User heap
#define HEAP_CLASS_5 0x00005000 // Console heap
#define HEAP_CLASS_6 0x00006000 // User desktop heap
#define HEAP_CLASS_7 0x00007000 // CSR shared heap
#define HEAP_CLASS_8 0x00008000 // CSR port heap
#define HEAP_CLASS_MASK 0x0000f000
#define DOS_DEVICE_PREFIX_LENGTH 64
#define GET_FUNCTION(x, y) GetProcAddress((x), y)
#define GET_NT_FUNCTION(x) GET_FUNCTION(NTDLL_MODULE, x)

typedef NTSTATUS (NTAPI *_NtQuerySystemInformation) 
(
  ULONG SystemInformationClass,
  PVOID SystemInformation,
  ULONG SystemInformationLength,
  PULONG ReturnLength
);
_NtQuerySystemInformation NtQuerySystemInformation = NULL;

typedef NTSTATUS (NTAPI *_NtDuplicateObject) 
(
  HANDLE SourceProcessHandle,
  HANDLE SourceHandle,
  HANDLE TargetProcessHandle,
  PHANDLE TargetHandle,
  ACCESS_MASK DesiredAccess,
  ULONG Attributes,
  ULONG Options
);
_NtDuplicateObject NtDuplicateObject = NULL;

typedef NTSTATUS (NTAPI *_NtQueryObject) 
(
  HANDLE ObjectHandle,
  ULONG ObjectInformationClass,
  PVOID ObjectInformation,
  ULONG ObjectInformationLength,
  PULONG ReturnLength
);
_NtQueryObject NtQueryObject = NULL;

typedef struct _UNICODE_STRING
{
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES
{
  ULONG Length;
  HANDLE RootDirectory;
  PUNICODE_STRING ObjectName;
  ULONG Attributes;
  PVOID SecurityDescriptor; // PSECURITY_DESCRIPTOR;
  PVOID SecurityQualityOfService; // PSECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef NTSTATUS (NTAPI *PRTL_HEAP_COMMIT_ROUTINE)
(
  PVOID Base,
  PVOID *CommitAddress,
  PSIZE_T CommitSize
);

typedef struct _RTL_HEAP_PARAMETERS
{
  ULONG Length;
  SIZE_T SegmentReserve;
  SIZE_T SegmentCommit;
  SIZE_T DeCommitFreeBlockThreshold;
  SIZE_T DeCommitTotalFreeThreshold;
  SIZE_T MaximumAllocationSize;
  SIZE_T VirtualMemoryThreshold;
  SIZE_T InitialCommit;
  SIZE_T InitialReserve;
  PRTL_HEAP_COMMIT_ROUTINE CommitRoutine;
  SIZE_T Reserved[2];
} RTL_HEAP_PARAMETERS, *PRTL_HEAP_PARAMETERS;

typedef struct _SYSTEM_HANDLE
{
  ULONG ProcessId;
  BYTE ObjectTypeNumber;
  BYTE Flags;
  USHORT Handle;
  PVOID Object;
  ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, *PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION
{
  ULONG HandleCount;
  SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

typedef enum _POOL_TYPE
{
  NonPagedPool,
  PagedPool,
  NonPagedPoolMustSucceed,
  DontUseThisType,
  NonPagedPoolCacheAligned,
  PagedPoolCacheAligned,
  NonPagedPoolCacheAlignedMustS
} POOL_TYPE, *PPOOL_TYPE;

typedef struct _OBJECT_TYPE_INFORMATION
{
  UNICODE_STRING Name;
  ULONG TotalNumberOfObjects;
  ULONG TotalNumberOfHandles;
  ULONG TotalPagedPoolUsage;
  ULONG TotalNonPagedPoolUsage;
  ULONG TotalNamePoolUsage;
  ULONG TotalHandleTableUsage;
  ULONG HighWaterNumberOfObjects;
  ULONG HighWaterNumberOfHandles;
  ULONG HighWaterPagedPoolUsage;
  ULONG HighWaterNonPagedPoolUsage;
  ULONG HighWaterNamePoolUsage;
  ULONG HighWaterHandleTableUsage;
  ULONG InvalidAttributes;
  GENERIC_MAPPING GenericMapping;
  ULONG ValidAccess;
  BOOLEAN SecurityRequired;
  BOOLEAN MaintainHandleCount;
  USHORT MaintainTypeList;
  POOL_TYPE PoolType;
  ULONG PagedPoolUsage;
  ULONG NonPagedPoolUsage;
} OBJECT_TYPE_INFORMATION, *POBJECT_TYPE_INFORMATION;

typedef NTSTATUS (NTAPI *_NtOpenSymbolicLinkObject)
(
  PHANDLE LinkHandle,
  ACCESS_MASK DesiredAccess,
  POBJECT_ATTRIBUTES ObjectAttributes
);
_NtOpenSymbolicLinkObject NtOpenSymbolicLinkObject = NULL;

typedef NTSTATUS (NTAPI *_NtQuerySymbolicLinkObject)
(
  HANDLE LinkHandle,
  PUNICODE_STRING LinkTarget,
  PULONG ReturnedLength
);
_NtQuerySymbolicLinkObject NtQuerySymbolicLinkObject = NULL;

typedef NTSTATUS (NTAPI *_NtClose) 
(
  HANDLE Handle
);
_NtClose NtClose = NULL;

typedef PVOID (NTAPI *_RtlCreateHeap) 
(
  ULONG Flags,
  PVOID HeapBase,
  SIZE_T ReserveSize,
  SIZE_T CommitSize,
  PVOID Lock,
  PRTL_HEAP_PARAMETERS Parameters
);
_RtlCreateHeap RtlCreateHeap = NULL;

typedef PVOID (NTAPI *_RtlDestroyHeap) 
(
  PVOID HeapHandle
);
_RtlDestroyHeap RtlDestroyHeap = NULL;

typedef PVOID (NTAPI *_RtlAllocateHeap) 
(
  PVOID HeapHandle,
  ULONG Flags,
  SIZE_T Size
);
_RtlAllocateHeap RtlAllocateHeap = NULL;

typedef BOOLEAN (NTAPI *_RtlFreeHeap)
(
  PVOID HeapHandle,
  ULONG Flags,
  PVOID BaseAddress
);
_RtlFreeHeap RtlFreeHeap = NULL;

typedef BOOLEAN (NTAPI *_RtlPrefixUnicodeString) 
(
  PUNICODE_STRING String1,
  PUNICODE_STRING String2,
  BOOLEAN CaseInSensitive
);
_RtlPrefixUnicodeString RtlPrefixUnicodeString = NULL;

HANDLE CURRENT_PROCESS_HANDLE;
HMODULE NTDLL_MODULE;
static UNICODE_STRING dosDevicePrefixes[26];
static PVOID heap;

int panic(const char *msg)
{
  fprintf(stderr, "%s\n", msg);
  return 1;
}

PVOID allocate(SIZE_T Size)
{
  return RtlAllocateHeap(heap, HEAP_GENERATE_EXCEPTIONS, Size);
}

int equalString(wchar_t *s1, wchar_t *s2, size_t len)
{
  size_t l;

  l = len & -2;

  if (l) {
    while (TRUE) {
      if (*(PULONG)s1 != *(PULONG)s2) {
        return 0;
      }

      s1 += 2;
      s2 += 2;
      l -= 2;

      if (!l) {
        break;
      }
    }
  }

  if (len & 1) {
    return *s1 == *s2;
  } else {
    return 1;
  }
}

BOOLEAN startsWithStringRef(
    PUNICODE_STRING String1,
    PUNICODE_STRING String2,
    BOOLEAN IgnoreCase)
{
  if (!IgnoreCase) {
    USHORT length2;
    length2 = String2->Length;
    if (String1->Length < length2) {
      return FALSE;
    }

    return equalString(String1->Buffer, String2->Buffer, length2 / sizeof(WCHAR));
  } else {
    return RtlPrefixUnicodeString(String2, String1, TRUE);
  }
}

void refreshDosDevicePrefixes()
{
  WCHAR deviceNameBuffer[7] = L"\\??\\ :";
  ULONG i;
  PUCHAR buffer;

  buffer = static_cast<PUCHAR>(allocate(DOS_DEVICE_PREFIX_LENGTH * sizeof(WCHAR) * 26));
  for (i = 0; i < 26; i++)
  {
    dosDevicePrefixes[i].Length = 0;
    dosDevicePrefixes[i].MaximumLength = DOS_DEVICE_PREFIX_LENGTH * sizeof(WCHAR);
    dosDevicePrefixes[i].Buffer = (PWSTR)buffer;
    buffer += DOS_DEVICE_PREFIX_LENGTH * sizeof(WCHAR);

    HANDLE linkHandle;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING deviceName;

    deviceNameBuffer[4] = (WCHAR)('A' + i);
    deviceName.Buffer = deviceNameBuffer;
    deviceName.Length = 6 * sizeof(WCHAR);

    InitializeObjectAttributes(
        &oa,
        &deviceName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    if (NT_SUCCESS(NtOpenSymbolicLinkObject(
            &linkHandle,
            SYMBOLIC_LINK_QUERY,
            &oa
            ))) {
      if (!NT_SUCCESS(NtQuerySymbolicLinkObject(
              linkHandle,
              &dosDevicePrefixes[i],
              NULL
              )))
      {
        dosDevicePrefixes[i].Length = 0;
      }

      NtClose(linkHandle);
    } else {
      dosDevicePrefixes[i].Length = 0;
    }
  }
}

PUNICODE_STRING resolveDosDevicePrefix(PUNICODE_STRING Name)
{
  ULONG i;
  PUNICODE_STRING newName = NULL;

  for (i = 0; i < 26; i++)
  {
    BOOLEAN isPrefix = FALSE;
    ULONG prefixLength;

    prefixLength = dosDevicePrefixes[i].Length;

    if (prefixLength != 0) {
      isPrefix = startsWithStringRef(Name, &dosDevicePrefixes[i], TRUE);
    }

    if (isPrefix)
    {
      unsigned int len = sizeof(UNICODE_STRING) + 3 * sizeof(WCHAR) + Name->Length - prefixLength;
      newName = (PUNICODE_STRING) malloc(len);
      memset(newName, 0, len);
      newName->Length = 2 + Name->Length - prefixLength;
      newName->MaximumLength = 0xFFFF;
      newName->Buffer = (PWSTR)(((char *) newName) + sizeof(UNICODE_STRING));
      newName->Buffer[0] = (WCHAR)('A' + i);
      newName->Buffer[1] = ':';
      memcpy(&newName->Buffer[2], 
          &Name->Buffer[prefixLength / sizeof(WCHAR)],
          Name->Length - prefixLength);

      break;
    }
  }

  return newName;
}


int printOpenFile(const char *exename, DWORD pid, 
    HANDLE hproc, vector<SYSTEM_HANDLE> handles)
{
  int rc = 0;
  bool hasOpenFile = false;

  NTSTATUS status;
  vector<SYSTEM_HANDLE>::iterator itend = handles.end();

  for (vector<SYSTEM_HANDLE>::iterator it = handles.begin();
      it != itend; ++it) {
    if (it->ProcessId != pid) {
      continue;
    }

    HANDLE hdup = NULL;
    POBJECT_TYPE_INFORMATION objtype;
    PVOID objinfo;
    UNICODE_STRING objname;
    ULONG len;

    if (!NT_SUCCESS(NtDuplicateObject(hproc, (HANDLE) it->Handle,
            CURRENT_PROCESS_HANDLE, &hdup, 0, 0, 0))) {
      fprintf(stderr, "Can't duplicate handle [%#x]!\n", it->Handle);
      rc = 1;
      continue;
    }

    objtype = (POBJECT_TYPE_INFORMATION)malloc(0x1000);
    if (!NT_SUCCESS(NtQueryObject(hdup, ObjectTypeInformation,
            objtype, 0x1000, NULL))) {
      fprintf(stderr, "Can't query object type of handle [%#x]\n", it->Handle);
      rc = 1;
      CloseHandle(hdup);
      continue;
    }

    if (it->GrantedAccess == 0x0012019f) {
      fprintf(stderr, "Have no access to handle [%#x]\n", it->Handle);
      free(objtype);
      CloseHandle(hdup);
      continue;
    }

    objinfo = malloc(0x1000);
    if (!NT_SUCCESS(NtQueryObject(hdup, ObjectNameInformation, 
          objinfo, 0x1000, &len))) {
      objinfo = realloc(objinfo, len);
      if (!NT_SUCCESS(NtQueryObject(hdup, ObjectNameInformation,
              objinfo, len, NULL)))
      {
        fprintf(stderr, "Can't query object name to handle [%#x]\n", it->Handle);
        free(objtype);
        free(objinfo);
        CloseHandle(hdup);
        continue;
      }
    }

    objname = *(PUNICODE_STRING)objinfo;
    if (objname.Length) {
      // all file objects mush have name
      if (wcscmp(objtype->Name.Buffer, L"File") == 0) {
        if (!hasOpenFile) {
          fprintf(stdout, INDENT "<Process name='%s' pid='%u'>\n", exename, pid);
          hasOpenFile = true;
        }
        PUNICODE_STRING newName = resolveDosDevicePrefix(&objname);
        if (newName) {
          fprintf(stdout, INDENT INDENT "<File handle='%#x'>%.*S</File>\n",
            it->Handle, newName->Length / 2, newName->Buffer);
          free(newName);
        } else {
          fprintf(stdout, INDENT INDENT "<File handle='%#x'>%.*S</File>\n",
            it->Handle, objname.Length / 2, objname.Buffer);
        }
      }
    }

    free(objtype);
    free(objinfo);
    CloseHandle(hdup);
  }
  if (hasOpenFile) {
    fprintf(stdout, INDENT "</Process>\n");
  }
  CloseHandle(hproc);

  return rc;
}

int checkProcess(const char **exename, unsigned int exenum, 
    DWORD pid, vector<SYSTEM_HANDLE> handles)
{
  char procname[MAX_PATH] = "<unknown>";
  HANDLE hproc = 
    OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

  int rc = 0;
  if (NULL != hproc) {
    HMODULE hmod;
    DWORD needed;

    if (EnumProcessModules(hproc, &hmod, sizeof(hmod), &needed)) {
      GetModuleBaseName(hproc, hmod, 
          procname, sizeof(procname)/sizeof(TCHAR));
    }

    if (exename != NULL && exenum > 0) {
      bool found = false;
      for (unsigned int idx = 0; idx < exenum; ++idx) {
        if (strcmp(exename[idx], procname) == 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        goto cleanup;
      }
    }
  
    rc = printOpenFile(procname, pid, hproc, handles);
  } else {
    rc = 1;
    fprintf(stderr, "Can't open process %u\n", pid);
  }

cleanup:
  CloseHandle(hproc);
  return rc;
}

int enumHandles(vector<SYSTEM_HANDLE> *handles)
{
  int rc = 0;

  NTSTATUS status;
  PSYSTEM_HANDLE_INFORMATION hinfo;
  ULONG hinfosz = 0x10000;

  hinfo = (PSYSTEM_HANDLE_INFORMATION)malloc(hinfosz);

  while ((status = NtQuerySystemInformation(
          SystemHandleInformation,
          hinfo,
          hinfosz,
          NULL
          )) == STATUS_INFO_LENGTH_MISMATCH) {
    hinfo = static_cast<PSYSTEM_HANDLE_INFORMATION>(realloc(hinfo, hinfosz *= 2));
  }

  if (!NT_SUCCESS(status))
  {
    rc = panic("NtQuerySystemInformation failed!");
    goto cleanup;
  }

  for (ULONG idx = 0; idx < hinfo->HandleCount; idx++)
  {
    handles->push_back(hinfo->Handles[idx]);
  }

cleanup:
  free(hinfo);

  return rc;
}

int loadFunctions()
{
  CAL_NT_FUNC(_NtQuerySystemInformation, NtQuerySystemInformation)
  CAL_NT_FUNC(_NtDuplicateObject, NtDuplicateObject)
  CAL_NT_FUNC(_NtQueryObject, NtQueryObject)
  CAL_NT_FUNC(_NtOpenSymbolicLinkObject, NtOpenSymbolicLinkObject)
  CAL_NT_FUNC(_NtQuerySymbolicLinkObject, NtQuerySymbolicLinkObject)
  CAL_NT_FUNC(_NtClose, NtClose)
  CAL_NT_FUNC(_RtlCreateHeap, RtlCreateHeap)
  CAL_NT_FUNC(_RtlDestroyHeap, RtlDestroyHeap)
  CAL_NT_FUNC(_RtlAllocateHeap, RtlAllocateHeap)
  CAL_NT_FUNC(_RtlFreeHeap, RtlFreeHeap)
  CAL_NT_FUNC(_RtlPrefixUnicodeString, RtlPrefixUnicodeString)
  return 0;
}

int setPrivilege(HANDLE htoken, LPCTSTR priv, BOOL enable)
{
  TOKEN_PRIVILEGES tp;
  LUID luid;
  TOKEN_PRIVILEGES prevtp;
  DWORD prevsz=sizeof(TOKEN_PRIVILEGES);

  if (!LookupPrivilegeValue( NULL, priv, &luid )) {
    PANIC("Could not get current privilege value")
  }

  tp.PrivilegeCount           = 1;
  tp.Privileges[0].Luid       = luid;
  tp.Privileges[0].Attributes = 0;

  AdjustTokenPrivileges(htoken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevtp, &prevsz);

  if (GetLastError() != ERROR_SUCCESS) {
    PANIC("Could not adjust token privilege")
  }

  prevtp.PrivilegeCount       = 1;
  prevtp.Privileges[0].Luid   = luid;

  if(enable) {
    prevtp.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
  } else {
    prevtp.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED & prevtp.Privileges[0].Attributes);
  }

  AdjustTokenPrivileges(htoken, FALSE, &prevtp, prevsz, NULL, NULL);

  if (GetLastError() != ERROR_SUCCESS) {
    PANIC("Could not adjust token privilege")
  }

  return 0;
}

int setDebugPrivilege()
{
  HANDLE htoken;
  const char *msg = NULL;
  if(!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &htoken))
  {
    if (GetLastError() == ERROR_NO_TOKEN)
    {
      if (!ImpersonateSelf(SecurityImpersonation)) {
        msg = PANIC_STRING("Could not impersonate");
        goto cleanup;
      }

      if(!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &htoken)){
        msg = PANIC_STRING("Could not open thread token");
        goto cleanup;
      }
    } else {
      msg = PANIC_STRING("Could not open thread token");
      goto cleanup;
    }
  }

  CHECK_SUCCESS(setPrivilege(htoken, SE_DEBUG_NAME, TRUE))

cleanup:
  CloseHandle(htoken);
  if (msg) {
    return panic(msg);
  }
  return 0;
}

int main(int argc, const char **argv)
{
  const char **exename = argc == 1 ? NULL : argv + 1;
  unsigned int exenum = argc - 1;

  CURRENT_PROCESS_HANDLE = GetCurrentProcess();
  NTDLL_MODULE = GetModuleHandleA("ntdll.dll");
  CHECK_SUCCESS(setDebugPrivilege())

  CHECK_SUCCESS(loadFunctions())

  heap = RtlCreateHeap(
    HEAP_GROWABLE | HEAP_CLASS_1,
    NULL, 2 * 1024 * 1024, 1024 * 1024, NULL, NULL
  );
  refreshDosDevicePrefixes();

  vector<SYSTEM_HANDLE> handles;
  CHECK_SUCCESS(enumHandles(&handles))

  unsigned int procnum = 1024;
  DWORD arraysz = sizeof(DWORD) * procnum;
  DWORD *procs = static_cast<DWORD *>(malloc(arraysz));
  CHECK_MEMORY(procs)

  do {
    DWORD needed = 0;
    if (!EnumProcesses(procs, arraysz, &needed)) {
      return panic("Call to EnumProcesses failed");
    }
    if (arraysz == needed) {
      procnum += 1024;
      arraysz = sizeof(DWORD) * procnum;
      procs = static_cast<DWORD *>(realloc(procs, arraysz));
      CHECK_MEMORY(procs)
    } else {
      procnum = needed / sizeof(DWORD);
      break;
    }
  } while (true);

  fprintf(stdout, "<OpenFiles>\n");
  for (unsigned int idx = 0; idx < procnum; ++idx) {
    if (procs[idx] != 0) {
      checkProcess(exename, exenum, procs[idx], handles);
    }
  }
  fprintf(stdout, "</OpenFiles>\n");

  return 0;
}
