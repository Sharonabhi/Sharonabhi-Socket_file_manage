#include "includes.h"
#include <termios.h>
#include <unistd.h>

// Check if the write command format is correct
bool correctWriteMode(char* write_mode) {    
    if (strlen(write_mode) != 1) return false;
    if (write_mode[0] != 'a' && write_mode[0] != 'o') return false;
    return true;
}

// Output server response
void print_server_response(int sock_fd) {
    Response res;
    int read_size;

    // Receive response from the server
    if ((read_size = recv(sock_fd, &res, sizeof(Response), 0)) > 0) {
        printf("[Server ]: %s\n", res.status);
        if (strlen(res.content) > 0) 
            printf("[Content]:\n%s\n", res.content);
        printf("\n");
    } 
    else {
        printf("Disconnected from server.\n");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
}

// Handle the write command
void handle_write(User* user, int user_fd, const char *command) {
    
    ClientRequest request;  // Package user data and command
    memcpy(&request.user, user, sizeof(User));  
    strcpy(request.command, command);    

    if (send(user_fd, &request, sizeof(ClientRequest), 0) < 0) {
        perror("Failed to send write command");
        return;
    }

    // Receive server response
    Response res;
    if (recv(user_fd, &res, sizeof(Response), 0) > 0) {

        // Output server response
        printf("[Server ]: %s\n", res.status);
        if (strlen(res.content) > 0) 
            printf("[Content]:\n\n%s\n", res.content);
        printf("\n");

        // Confirm the server is ready for writing to the file
        if (!strcmp(res.status, "Ready for writing the file")) {
            
            // Enter content editing mode
            printf("Enter the content to write. \n(Press `Ctrl+q` and `Enter` to finish editing):\n");

            struct termios oldt, newt;
            char content[CONTENT_SIZE] = {0};
            char c;
            int pos = 0;

            // Set terminal to raw mode to capture Ctrl+q
            tcgetattr(STDIN_FILENO, &oldt); // Get current terminal attributes
            newt = oldt;
            newt.c_iflag &= ~(IXON);        // Disable flow control (prevent Ctrl+q from pausing input)
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);

            printf("\n"); 

            // Start writing content
            while (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 17) { // Ctrl+q
                    break;
                } else if (c == 127) {          // Handle backspace
                    if (pos > 0) {
                        printf("\b \b");        // Delete backward
                        content[--pos] = '\0';
                    }
                } else {                        // Save characters
                    if (pos < CONTENT_SIZE - 1) content[pos++] = c;
                }
            }

            // Restore terminal settings
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

            if (send(user_fd, content, pos, 0) < 0) perror("Failed to send content");
            print_server_response(user_fd);
        }
        else return;
    } 
    else  perror("Failed to receive server response");
    return;
}

// Handle client interaction
void handle_client(int user_fd, User *user) {
    char command[BUFFER_SIZE];
    char filename[256], write_mode[2];

    while (1) {
        printf("%s > ", user->name);
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL) {
            printf("Error reading input. Exiting.\n");
            break;
        }
        command[strcspn(command, "\n")] = '\0'; // Remove newline character from input
        
        // Commands on the client side
        if (!strcmp(command, "exit")) {         // exit
            printf("Closing connection...\n");
            break;
        }
        else if (!strcmp(command, "help")) {     // List the commands
            printf("\nHere are the commands:\n");
            printf("=====================================================================================\n");
            printf(" exit:\t\t\t\t\tclose the connection.\n");
            printf(" create [filename] [permissions]:\tcreate a file. (permissions ex: r-rw--).\n");
            printf(" mode   [filename] [permissions]:\tchange the permission of the file.\n");
            printf(" write  [filename] [mode]:\t\twrite a file. mode o/a means overwrite/append.\n");
            printf(" read   [filename]:\t\t\tget the content of the file.\n");
            printf(" ls:  \t\t\t\t\tlist all the files that can be read/written.\n");
            printf("=====================================================================================\n\n");
        }
        else if (!strcmp(command, "ls")) {      // List all readable/writable files
            ClientRequest request;  // Package user data and command
            memcpy(&request.user, user, sizeof(User));  
            strcpy(request.command, command);    

            if (send(user_fd, &request, sizeof(ClientRequest), 0) < 0) {
                perror("Send failed");
                break;
            }

            // Receive server response
            Response res;
            if (recv(user_fd, &res, sizeof(Response), 0) > 0) {
                if(!strcmp(res.status, "List accessible files successful")){
                    char *token, *filename, *permissions;

                    printf("Filename       \tPermission\n");
                    printf("===============================\n");
                    
                    // Parse the list of accessible files (ex: file1, rwr---|file2, rw----|)
                    token = strtok(res.content, ",");
                    while(token != NULL){
                        filename = token;
                        permissions = strtok(NULL, "|");

                        // Output the list of accessible files
                        printf("%-15s\t%s\n", filename, permissions);

                        token = strtok(NULL, ",");
                    }
                    printf("\n");
                }
            } 
            else  perror("Failed to receive server response");
        }
        else if (sscanf(command, "write %s %s", filename, write_mode) == 2){     // write
            if (correctWriteMode(write_mode)){
                handle_write(user, user_fd, command);
                continue;
            }
            else {
                printf("[Server]: Invalid command\n\n");
                continue;
            }
        }
        else {  // Other commands (read, mode, ...)
            ClientRequest request;  // Package user data and command
            memcpy(&request.user, user, sizeof(User));  
            strcpy(request.command, command);    

            if (send(user_fd, &request, sizeof(ClientRequest), 0) < 0) {
                perror("Send failed");
                break;
            }
            
            print_server_response(user_fd); // Output server response
        }
    }

    close(user_fd);
}

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;

    char username[256], userGroup[50];
    printf("Please input your name: ");     scanf("%s", username);
    printf("Please input your group: ");    scanf("%s", userGroup);

    // Create client socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    User user;
    strcpy(user.name, username);
    strcpy(user.group, userGroup);

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server at port %d\n", PORT);

    // Handle client interaction
    handle_client(sock_fd, &user);

    printf("Connection closed.\n");
    return 0;
}
