/*
* Windbg extension for VM traffic manipulation and analysis
* 
* Copyright 2011, isciurus. All rights reserved.
* 
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
* 
* - Redistributions of source code must retain the above copyright notice, this
*   list of conditions and the following disclaimer.
*
* - Redistributions in binary form must reproduce the above copyright notice, this
*   list of conditions and the following disclaimer in the documentation and/or
*   other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
* OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include "dbgexts.h"
#include <strsafe.h>
#include "pcap.h"
#include "windbgshark.h"

IDebugClient* pDebugClient;
IDebugControl* pDebugControl;

PDEBUG_CLIENT4        g_ExtClient;
PDEBUG_CONTROL        g_ExtControl;
PDEBUG_SYMBOLS2       g_ExtSymbols;

WINDBG_EXTENSION_APIS   ExtensionApis;

ULONG   TargetMachine;
BOOL    Connected;

HRESULT setBreakpoints(PDEBUG_CONTROL Control);
HRESULT removeBreakpoints(PDEBUG_CONTROL Control);

// Queries for all debugger interfaces.
extern "C" HRESULT
ExtQuery(PDEBUG_CLIENT4 Client)
{
    HRESULT Status;

    if ((Status = Client->QueryInterface(__uuidof(IDebugControl),
                                 (void **)&g_ExtControl)) != S_OK)
    {
        goto Fail;
    }
    if ((Status = Client->QueryInterface(__uuidof(IDebugSymbols2),
                                (void **)&g_ExtSymbols)) != S_OK)
    {
    goto Fail;
    }
    g_ExtClient = Client;

    return S_OK;

 Fail:
    ExtRelease();
    return Status;
}

// Cleans up all debugger interfaces.
void
ExtRelease(void)
{
    g_ExtClient = NULL;
    EXT_RELEASE(g_ExtControl);
    EXT_RELEASE(g_ExtSymbols);
}


// Normal output.
void __cdecl
ExtOut(PCSTR Format, ...)
{
    va_list Args;

    va_start(Args, Format);
    g_ExtControl->OutputVaList(DEBUG_OUTPUT_NORMAL, Format, Args);
    va_end(Args);
}

// Error output.
void __cdecl
ExtErr(PCSTR Format, ...)
{
    va_list Args;

    va_start(Args, Format);
    g_ExtControl->OutputVaList(DEBUG_OUTPUT_ERROR, Format, Args);
    va_end(Args);
}

// Warning output.
void __cdecl
ExtWarn(PCSTR Format, ...)
{
    va_list Args;

    va_start(Args, Format);
    g_ExtControl->OutputVaList(DEBUG_OUTPUT_WARNING, Format, Args);
    va_end(Args);
}

extern "C"
HRESULT
CALLBACK
DebugExtensionInitialize(PULONG Version, PULONG Flags)
{
    HRESULT Hr;

    *Version = DEBUG_EXTENSION_VERSION(1, 0);
    *Flags = 0;
    Hr = S_OK;

    if ((Hr = DebugCreate(__uuidof(IDebugClient),
                          (void **)&pDebugClient)) != S_OK)
    {
        return Hr;
    }

    if ((Hr = pDebugClient->QueryInterface(__uuidof(IDebugControl),
                                  (void **)&pDebugControl)) == S_OK)
    {

        //
        // Get the windbg-style extension APIS
        //
        ExtensionApis.nSize = sizeof (ExtensionApis);
        Hr = pDebugControl->GetWindbgExtensionApis64(&ExtensionApis);
    }

	windbgsharkInit();

    return Hr;
}


extern "C"
void
CALLBACK
DebugExtensionNotify(ULONG Notify, ULONG64 Argument)
{
    UNREFERENCED_PARAMETER(Argument);

    //
    // The first time we actually connect to a target
    //

    if ((Notify == DEBUG_NOTIFY_SESSION_ACCESSIBLE) && (!Connected))
    {
        IDebugClient *DebugClient;
        HRESULT Hr;
        PDEBUG_CONTROL DebugControl;

        if ((Hr = DebugCreate(__uuidof(IDebugClient),
                              (void **)&DebugClient)) == S_OK)
        {
            //
            // Get the architecture type.
            //

            if ((Hr = DebugClient->QueryInterface(__uuidof(IDebugControl),
                                       (void **)&DebugControl)) == S_OK)
            {
                if ((Hr = DebugControl->GetActualProcessorType(
                                             &TargetMachine)) == S_OK)
                {
                    Connected = TRUE;
                }

                DebugControl->Release();
            }

            DebugClient->Release();
        }
    }


    if (Notify == DEBUG_NOTIFY_SESSION_INACTIVE)
    {
        Connected = FALSE;
        TargetMachine = 0;
    }

    return;
}

extern "C"
void
CALLBACK
DebugExtensionUninitialize(void)
{
	windbgsharkUninitialize();
    return;
}

extern "C"
HRESULT
CALLBACK
KnownStructOutput(
    __in ULONG Flag,
    __in ULONG64 Address,
    __in PSTR StructName,
    __out_ecount(BufferSize) PSTR Buffer,
    __in PULONG BufferSize
    )
{
    const char* KnownStructs[] = {"_LARGE_INTEGER", "_SYSTEMTIME", NULL};
    HRESULT Hr;


    Hr = S_OK;

    if (Flag == DEBUG_KNOWN_STRUCT_GET_NAMES)
    {
        //
        // Return names of known structs in multi string
        //
        ULONG SizeRemaining = *BufferSize, SizeNeeded = 0, Length;
        PCHAR CopyAt = Buffer;

        for (ULONG i=0; KnownStructs[i] != NULL; i++)
        {
            if (SizeRemaining > (Length = (ULONG)strlen(KnownStructs[i]) + 1) &&
                Hr == S_OK)
            {
                Hr = StringCbCopy(CopyAt, SizeRemaining, KnownStructs[i]);

                SizeRemaining -= Length;
                CopyAt += Length;
            } else
            {
                Hr = S_FALSE;
            }
            SizeNeeded += Length;
        }
        // Terminate multistring and return size copied
        *CopyAt = 0;
        *BufferSize = SizeNeeded+1;
    } else if (Flag == DEBUG_KNOWN_STRUCT_GET_SINGLE_LINE_OUTPUT)
    {
        if (!strcmp(StructName, KnownStructs[0]))
        {
            ULONG64 Data;
            ULONG ret;

            if (ReadMemory(Address, &Data, sizeof(Data), &ret))
            {
                Hr = StringCbPrintf(Buffer, *BufferSize, " { %lx`%lx }", (ULONG) (Data >> 32), (ULONG) Data);
            } else
            {
                Hr = E_INVALIDARG;
            }
        } else if (!strcmp(StructName, KnownStructs[1]))
        {
                SYSTEMTIME Data;
                ULONG ret;

                if (ReadMemory(Address, &Data, sizeof(Data), &ret))
                {
                    Hr = StringCbPrintf(Buffer, *BufferSize, " { %02ld:%02ld:%02ld %02ld/%02ld/%04ld }",
                                        Data.wHour,
                                        Data.wMinute,
                                        Data.wSecond,
                                        Data.wMonth,
                                        Data.wDay,
                                        Data.wYear);
                } else
                {
                    Hr = E_INVALIDARG;
                }
            } else
        {
            Hr = E_INVALIDARG;
        }
    } else if (Flag == DEBUG_KNOWN_STRUCT_SUPPRESS_TYPE_NAME)
    {
        if (!strcmp(StructName, KnownStructs[0]))
        {
            // Do not print type name for KnownStructs[0]
            Hr = S_OK;
        }
        else
        {
            // Print the type name
            Hr = S_FALSE;
        }
    } else
    {
        Hr = E_INVALIDARG;
    }
    return Hr;
}

extern "C"
HRESULT
_EFN_Analyze(
    __in PDEBUG_CLIENT4 Client,
    __in FA_EXTENSION_PLUGIN_PHASE CallPhase,
    __in PDEBUG_FAILURE_ANALYSIS2 pAnalysis
    )
{
    INIT_API();

// Analysis tags
#define FA_TAG_SAMPLE_PLUGIN_DEBUG_TEXT 0xA0000000

    ExtOut("DbgExts Analysis Phase: %lx\n", CallPhase);
    switch (CallPhase)
    {
    case FA_PLUGIN_STACK_ANALYSIS:
        pAnalysis->SetString((FA_TAG) FA_TAG_SAMPLE_PLUGIN_DEBUG_TEXT,
                             "Sample custom analyzer was run for this analysis.\n");
        break;
    case FA_PLUGIN_POST_BUCKETING:
        PFA_ENTRY Entry;

        //
        // Set default bucket if folowup module in dbgeng
        //
        if ((Entry = pAnalysis->Get(DEBUG_FLR_MODULE_NAME)) != NULL &&
            !strcmp(FA_ENTRY_DATA(PSTR, Entry), "dbgeng"))
        {
            pAnalysis->SetString(DEBUG_FLR_DEFAULT_BUCKET_ID, "AV_IN_DEBUGGER");
        }
        break;
    default:
        // do nothing
        EXIT_API();
        return S_OK;
    }
    UNREFERENCED_PARAMETER(pAnalysis);

    EXIT_API();
    return S_OK;
}

