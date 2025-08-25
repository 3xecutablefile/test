#include <DriverKit/DriverKit.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <poll.h> // For polling socket
#include <thread> // For std::thread

// Function to handle the control socket communication
void control_socket_thread() {
    int sock_fd;
    struct sockaddr_un addr;
    char buffer[256];
    ssize_t bytes_read;

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        std::cerr << "BlackhatOS: Control socket creation failed." << std::endl;
        return;
    }

    unlink("/tmp/blackhatctl"); // Remove any old socket file

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/blackhatctl", sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "BlackhatOS: Control socket bind failed." << std::endl;
        close(sock_fd);
        return;
    }

    if (listen(sock_fd, 1) == -1) { // Listen for one connection
        std::cerr << "BlackhatOS: Control socket listen failed." << std::endl;
        close(sock_fd);
        return;
    }

    std::cout << "BlackhatOS: Waiting for control socket connection..." << std::endl;
    int client_fd = accept(sock_fd, NULL, NULL); // Accept connection
    if (client_fd == -1) {
        std::cerr << "BlackhatOS: Failed to accept control socket connection." << std::endl;
        close(sock_fd);
        return;
    }

    std::cout << "BlackhatOS: Control socket connected. Reading config..." << std::endl;
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        std::cout << "BlackhatOS: Received config: " << buffer << std::endl;
    }
    std::cout << "BlackhatOS: Control socket disconnected." << std::endl;

    close(client_fd);
    close(sock_fd);
}

kern_return_t Start() {
    std::cout << "BlackhatOS mac driver loaded 🎩⚡" << std::endl;

    // Start the control socket thread
    std::thread control_thread(control_socket_thread);
    control_thread.detach(); // Detach to run in background

    // --- Console Banner MVP & Fake Shell ---
    int sock_fd;
    struct sockaddr_un addr;
    const char* banner = 
        "d8888b. db       .d8b.   .o88b. db   dD db   db  .d8b.  d888888b  .d88b.  .d8888. \n"
        "88  `8D 88      d8' `8b d8P  Y8 88 ,8P' 88   88 d8' `8b `~~88~~' .8P  Y8. 88'  YP \n"
        "88oooY' 88      88ooo88 8P      88,8P   88ooo88 88ooo88    88    88    88 `8bo.   \n"
        "88~~~b. 88      88~~~88 8b      88`8b   88~~~88 88~~~88    88    88    88   `Y8b. \n"
        "88   8D 88booo. 88   88 Y8b  d8 88 `88. 88   88 88   88    88    `8b  d8' db   8D \n"
        "Y8888P' Y88888P YP   YP  `Y88P' YP   YD YP   YP YP   YP    YP     `Y88P'  `8888Y'\n";
    const char* prompt = "\n┌──(root@BlackhatOS)-[~]\n└─⚡ ";
    char consoleBuffer[256];
    ssize_t consoleBytesRead;

    // Create Unix domain socket for console
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        std::cerr << "BlackhatOS: Failed to create console socket." << std::endl;
        return KERN_FAILURE;
    }

    unlink("/tmp/blackhat-console"); // Remove any old socket file

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/blackhat-console", sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "BlackhatOS: Failed to bind console socket." << std::endl;
        close(sock_fd);
        return KERN_FAILURE;
    }

    if (listen(sock_fd, 1) == -1) { // Listen for one connection
        std::cerr << "BlackhatOS: Failed to listen on console socket." << std::endl;
        close(sock_fd);
        return KERN_FAILURE;
    }

    std::cout << "BlackhatOS: Waiting for console socket connection..." << std::endl;
    int client_fd = accept(sock_fd, NULL, NULL); // Accept connection
    if (client_fd == -1) {
        std::cerr << "BlackhatOS: Failed to accept console socket connection." << std::endl;
        close(sock_fd);
        return KERN_FAILURE;
    }

    std::cout << "BlackhatOS: Console socket connected. Writing banner..." << std::endl;
    write(client_fd, banner, strlen(banner));

    // Fake shell loop
    while (true) {
        write(client_fd, prompt, strlen(prompt)); // Write prompt

        // Read user input
        consoleBytesRead = read(client_fd, consoleBuffer, sizeof(consoleBuffer) - 1);
        if (consoleBytesRead > 0) {
            consoleBuffer[consoleBytesRead] = '\0'; // Null-terminate
            std::cout << "BlackhatOS: Console received: " << consoleBuffer << std::endl; // For driver debug

            // Check for "exit" command
            if (strncmp(consoleBuffer, "exit", 4) == 0 || strncmp(consoleBuffer, "quit", 4) == 0) {
                write(client_fd, "\nExiting BlackhatOS shell.\n", strlen("\nExiting BlackhatOS shell.\n"));
                break;
            }

            // Echo input back
            write(client_fd, "\n", 1);
            write(client_fd, consoleBuffer, consoleBytesRead);
            write(client_fd, "\n", 1);
        } else if (consoleBytesRead == 0) {
            // Client disconnected
            std::cout << "BlackhatOS: Console client disconnected." << std::endl;
            break;
        } else {
            // Error reading
            std::cerr << "BlackhatOS: Error reading from console socket." << std::endl;
            break;
        }
    }
    std::cout << "BlackhatOS: Console shell loop ended." << std::endl;

    close(client_fd);
    close(sock_fd);
    // --- End Console Banner MVP & Fake Shell ---

    // TODO: shared memory to guest kernel
    // TODO: stub tun device

    return KERN_SUCCESS;
}