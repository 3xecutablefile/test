#include <ntddk.h>
#include <windows.h> // For named pipe functions
#include <string.h>  // For strlen, strcmp

// Function to handle the control pipe communication
DWORD WINAPI ControlPipeThread(LPVOID lpParam) {
    HANDLE hControlPipe;
    DWORD dwRead;
    char buffer[256];

    hControlPipe = CreateNamedPipeA(
        "\\.\\pipe\\blackhatctl",      // Pipe name
        PIPE_ACCESS_DUPLEX,             // Read and write access
        PIPE_TYPE_BYTE | PIPE_WAIT,     // Byte stream, blocking mode
        1,                              // Max instances
        0,                              // Out buffer size (default)
        0,                              // In buffer size (default)
        0,                              // Default timeout
        NULL                            // Default security attributes
    );

    if (hControlPipe == INVALID_HANDLE_VALUE) {
        DbgPrint("BlackhatOS: Control pipe creation failed. Error: %lu\n", GetLastError());
        return 1;
    }

    DbgPrint("BlackhatOS: Waiting for control pipe connection...\n");
    if (ConnectNamedPipe(hControlPipe, NULL)) {
        DbgPrint("BlackhatOS: Control pipe connected. Reading config...\n");
        while (ReadFile(hControlPipe, buffer, sizeof(buffer) - 1, &dwRead, NULL) && dwRead > 0) {
            buffer[dwRead] = '\0';
            DbgPrint("BlackhatOS: Received config: %s\n", buffer);
            // Acknowledge receipt on console pipe (conceptual)
            // In a real driver, this would be more sophisticated
            // For now, just DbgPrint
        }
        DbgPrint("BlackhatOS: Control pipe disconnected.\n");
    } else {
        DbgPrint("BlackhatOS: Failed to connect control pipe. Error: %lu\n", GetLastError());
    }

    DisconnectNamedPipe(hControlPipe);
    CloseHandle(hControlPipe);
    return 0;
}


DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("BlackhatOS driver loaded 🎩⚡\n");

    // Start the control pipe thread
    HANDLE hThread = CreateThread(
        NULL,                   // default security attributes
        0,                      // default stack size
        ControlPipeThread,      // thread function name
        NULL,                   // argument to thread function
        0,                      // default creation flags
        NULL);                  // receive thread identifier

    if (hThread == NULL) {
        DbgPrint("BlackhatOS: Failed to create control pipe thread. Error: %lu\n", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }
    CloseHandle(hThread); // Don't need the handle to the thread

    // --- Console Banner MVP & Fake Shell ---
    HANDLE hConsolePipe;
    DWORD dwWritten;
    DWORD dwRead;
    char consoleBuffer[256];
    const char* banner = 
        "d8888b. db       .d8b.   .o88b. db   dD db   db  .d8b.  d888888b  .d88b.  .d8888. \n"
        "88  `8D 88      d8' `8b d8P  Y8 88 ,8P' 88   88 d8' `8b `~~88~~' .8P  Y8. 88'  YP \n"
        "88oooY' 88      88ooo88 8P      88,8P   88ooo88 88ooo88    88    88    88 `8bo.   \n"
        "88~~~b. 88      88~~~88 8b      88`8b   88~~~88 88~~~88    88    88    88   `Y8b. \n"
        "88   8D 88booo. 88   88 Y8b  d8 88 `88. 88   88 88   88    88    `8b  d8' db   8D \n"
        "Y8888P' Y88888P YP   YP  `Y88P' YP   YD YP   YP YP   YP    YP     `Y88P'  `8888Y'\n";
    const char* prompt = "\n┌──(root@BlackhatOS)-[~]\n└─⚡ ";

    // Create the named pipe for console I/O (bidirectional)
    hConsolePipe = CreateNamedPipeA(
        "\\.\\pipe\\blackhat-console", // Pipe name
        PIPE_ACCESS_DUPLEX,             // Read and write access
        PIPE_TYPE_BYTE | PIPE_WAIT,     // Byte stream, blocking mode
        1,                              // Max instances
        0,                              // Out buffer size (default)
        0,                              // In buffer size (default)
        0,                              // Default timeout
        NULL                            // Default security attributes
    );

    if (hConsolePipe == INVALID_HANDLE_VALUE) {
        DbgPrint("BlackhatOS: Failed to create console pipe. Error: %lu\n", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    DbgPrint("BlackhatOS: Waiting for console pipe connection...\n");
    if (ConnectNamedPipe(hConsolePipe, NULL)) {
        DbgPrint("BlackhatOS: Console pipe connected. Writing banner...\n");
        WriteFile(hConsolePipe, banner, strlen(banner), &dwWritten, NULL);

        // Fake shell loop
        while (TRUE) {
            WriteFile(hConsolePipe, prompt, strlen(prompt), &dwWritten, NULL); // Write prompt
            
            // Read user input
            if (ReadFile(hConsolePipe, consoleBuffer, sizeof(consoleBuffer) - 1, &dwRead, NULL) && dwRead > 0) {
                consoleBuffer[dwRead] = '\0'; // Null-terminate the string
                DbgPrint("BlackhatOS: Console received: %s\n", consoleBuffer); // For driver debug

                // Check for "exit" command (simple stub)
                if (strncmp(consoleBuffer, "exit", 4) == 0 || strncmp(consoleBuffer, "quit", 4) == 0) {
                    WriteFile(hConsolePipe, "\nExiting BlackhatOS shell.\n", strlen("\nExiting BlackhatOS shell.\n"), &dwWritten, NULL);
                    break;
                }

                // Echo input back (simulating command execution)
                WriteFile(hConsolePipe, "\n", 1, &dwWritten, NULL); // Newline after command
                WriteFile(hConsolePipe, consoleBuffer, dwRead, &dwWritten, NULL); // Echo command
                WriteFile(hConsolePipe, "\n", 1, &dwWritten, NULL); // Another newline
            } else {
                // ReadFile failed or pipe disconnected
                DbgPrint("BlackhatOS: Console ReadFile failed or pipe disconnected. Error: %lu\n", GetLastError());
                break;
            }
        }
        DbgPrint("BlackhatOS: Shell loop ended.\n");

    } else {
        DbgPrint("BlackhatOS: Failed to connect console pipe. Error: %lu\n", GetLastError());
    }

    DisconnectNamedPipe(hConsolePipe);
    CloseHandle(hConsolePipe);
    // --- End Console Banner MVP & Fake Shell ---

    // TODO: allocate shared memory region for Linux guest
    // TODO: stub TAP/TUN device

    return STATUS_SUCCESS;
}