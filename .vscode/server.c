#include "includes.h"
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/stat.h>

#define MAX_CLIENTS   15    // Maximum client connection count
#define MAX_FD 200          // Maximum FD value
#define MAX_FILES_NUM 100   // Maximum number of files
#define PERMISSION_LEN 6    // Permission length
#define FILE_DIRECTORY "./files"    // File storage path

// management Capability Lists
typedef struct {
    char filename[256];       // Filename
    char owner[50];           // File owner
    char group[50];           // File group
    char permissions[6];      // Permissions rwrwrw (owner, group, others)
    off_t size;               // File size
    char last_modified[20];   // last date modified time (ex: 2024/12/08 09:31)
    bool isModified;          // currently being modified
} Capability;

Capability file_list[MAX_FILES_NUM];    // store file information
int file_num = 0;                       // current file count

volatile int server_running = 1;                        // server running status

// 格式化結果
void format_result(Response *res, const char *status, const char *content) {
    strncpy(res->status, status, sizeof(res->status) - 1);
    res->status[sizeof(res->status) - 1] = '\0';  // ensure string termination

    if (content) {
        strncpy(res->content, content, sizeof(res->content) - 1);
        res->content[sizeof(res->content) - 1] = '\0';  // ensure string ending
    } 
    else 
        res->content[0] = '\0'; 
}

// check permission format
bool correctPermissionFormat(char* permission){
    if (strlen(permission) != PERMISSION_LEN) return false;
    for (int i = 0; i < PERMISSION_LEN; i++) {
        if(i % 2 == 0) {
            if (permission[i] != 'r' && permission[i] != '-') return false;
        }
        else
            if (permission[i] != 'w' && permission[i] != '-') return false;
    }
    return true;
}

// log record
void log_add(const char* user, const char* action, const char* filename, const char* status) {
    
    char timeNow[20];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(timeNow, sizeof(timeNow), "%Y/%m/%d %H:%M", tm_info);

    FILE *log = fopen("socket.log", "a");
    fprintf(log, "[%s] User: %s,\tAction: %s,\tFile: %s,\tStatus: %s\n", timeNow, user, action, filename, status);
    fclose(log);
}

// list accessible files
void list_file(int client_fd, User client){

    char accessiabled_files[CONTENT_SIZE] = "";

    for(int i = 0; i < file_num; i++){
        if (// allow access for everyone
            (file_list[i].permissions[4] == 'r') ||  (file_list[i].permissions[5] == 'w') ||
            // The group the client belongs to has read permissions
            (!strcmp(file_list[i].group, client.group) && file_list[i].permissions[2] == 'r') ||
            // The group the client belongs to has editing permissions
            (!strcmp(file_list[i].group, client.group) && file_list[i].permissions[3] == 'w') ||
            // client is the owner
            (!strcmp(file_list[i].owner, client.name)) ){
            
            strcat(accessiabled_files, file_list[i].filename);
            strcat(accessiabled_files, ",");
            strcat(accessiabled_files, file_list[i].permissions);
            strcat(accessiabled_files, "|");
        }
    }
    strcat(accessiabled_files, "\0");

    Response res;
    format_result(&res, "List accessiable files successful", accessiabled_files);
    send(client_fd, &res, sizeof(res), 0);
}

// Create profile
void create_file(int client_fd, User client, const char* filename, const char* permissions) {

    // Check if the file exists
    for (int i = 0; i < file_num; i++) {
        if (!strcmp(file_list[i].filename, filename)) { // found
            Response res;
            format_result(&res, "File already exists", "");   
            send(client_fd, &res, sizeof(res), 0);
            return;
        }
    }

    // File does not exist --> Create
    if (file_num < MAX_FILES_NUM) {

        // Make sure the folder where the file is stored exists
        struct stat st;
        if (stat(FILE_DIRECTORY, &st) == -1) 
            mkdir(FILE_DIRECTORY, 0700);  // Create the directory if it does not exist

        // create file
        char filepath[512];  // File path
        snprintf(filepath, sizeof(filepath), "%s//%s", FILE_DIRECTORY, filename);

        FILE *file = fopen(filepath, "w");
        if (file == NULL) {
            perror("Failed to create file");

            Response res;
            format_result(&res, "Failed to create file", "");
            send(client_fd, &res, sizeof(res), 0);

            return;
        }
        fclose(file);

        // renew file list
        strcpy(file_list[file_num].filename, filename);
        strcpy(file_list[file_num].permissions, permissions);
        strcpy(file_list[file_num].owner, client.name);
        strcpy(file_list[file_num].group, client.group);
        file_list[file_num].size = 0; 
        file_list[file_num].isModified = false;  

        // Set last modified time
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        strftime(file_list[file_num].last_modified, sizeof(file_list[file_num].last_modified), "%Y/%m/%d %H:%M", tm_info);

        file_num++;
        log_add(client.name, "create", filename, "success");

        Response res;
        format_result(&res, "File created successfully", "");
        send(client_fd, &res, sizeof(res), 0);
    } 
    else {
        Response res;
        format_result(&res, "File limit reached", "");
        send(client_fd, &res, sizeof(res), 0);
    }
}

// Read file
void read_file(int client_fd, User client, const char* filename) {

    for (int i = 0; i < file_num; i++) {
        if (!strcmp(file_list[i].filename, filename)) { // There is this file

            if( // Open to everyone
                (file_list[i].permissions[4] == 'r') ||  
                // The group the client belongs to has read permissions
                (!strcmp(file_list[i].group, client.group) && file_list[i].permissions[2] == 'r') ||
                // client is the owner
                (!strcmp(file_list[i].owner, client.name)) ){

                if(file_list[i].isModified){
                    Response res;
                    format_result(&res, "File is modifying", "");
                    send(client_fd, &res, sizeof(res), 0);
                    return;
                }

                char filepath[512];  // file path
                snprintf(filepath, sizeof(filepath), "%s//%s", FILE_DIRECTORY, filename);

                FILE *file = fopen(filepath, "r");
                if (file == NULL) { // Unable to open file
                    perror("Failed to open file");
                    Response res;
                    format_result(&res, "Failed to read file", "");
                    send(client_fd, &res, sizeof(res), 0);
                    log_add(client.name, "read", filename, "failed");
                } 
                else {              // File opened successfully
                    char file_content[CONTENT_SIZE];
                    size_t read_size = fread(file_content, 1, CONTENT_SIZE - 1, file);
                    file_content[read_size] = '\0';  // Make sure it ends with a string
                    fclose(file);
                    
                    Response res;
                    format_result(&res, "File read successful", file_content);
                    send(client_fd, &res, sizeof(res), 0);
                    log_add(client.name, "read", filename, "success");
                }
            }
            else {  
                Response res;
                format_result(&res, "Permission denied", "");
                send(client_fd, &res, sizeof(res), 0);
                log_add(client.name, "read", filename, "permission denied");
            }
            return;
        }
    }

    // file not found
    Response res;
    format_result(&res, "File not found", "");
    if (send(client_fd, &res, sizeof(Response), 0) < 0) 
        perror("Send failed");
}

// write file
void write_file(int client_fd, User client, const char* filename, const char* write_mode) {

    for (int i = 0; i < file_num; i++) {
        if (!strcmp(file_list[i].filename, filename)) { // found
            if(file_list[i].isModified){
                Response res;
                format_result(&res, "File is modifying", "");
                send(client_fd, &res, sizeof(res), 0);
                return;
            }

            file_list[i].isModified = true;

            if( // Open to everyone to write
                (file_list[i].permissions[5] == 'w') ||  
                // The group the client belongs to has write permissions
                (!strcmp(file_list[i].group, client.group) && file_list[i].permissions[3] == 'w') ||
                // client is the owner
                (!strcmp(file_list[i].owner, client.name)) ){

                char filepath[512];  // file path
                snprintf(filepath, sizeof(filepath), "%s//%s", FILE_DIRECTORY, filename);
                
                FILE *file;
                Response res;

                // Send preparation message to Client
                file = fopen(filepath, "r");
                if (file == NULL) { // cannot open file
                    perror("Failed to open file");
                    Response res;
                    format_result(&res, "Failed to get file content", "");
                    send(client_fd, &res, sizeof(res), 0);
                    log_add(client.name, "write", filename, "failed");
                } 
                else {              // File opened successfully
                    char file_content[CONTENT_SIZE];
                    size_t read_size = fread(file_content, 1, CONTENT_SIZE - 1, file);
                    file_content[read_size] = '\0';  // Make sure it ends with a string
                    fclose(file);

                    Response res;
                    format_result(&res, "Ready for writing the file", file_content);
                    send(client_fd, &res, sizeof(res), 0);
                }

                // receive content from client
                char content[CONTENT_SIZE];
                int read_size = recv(client_fd, content, sizeof(content) - 1, 0);
                if (read_size <= 0) {
                    perror("Failed to receive content");
                    format_result(&res, "Failed to receive content", "");
                    send(client_fd, &res, sizeof(res), 0);
                    log_add(client.name, "write", filename, "failed");
                    file_list[i].isModified = false;
                    return;
                }
                content[read_size] = '\0';  // make sure the string ends

                // write file
                if (!strcmp(write_mode, "o")){  // overwrite
                    file = fopen(filepath, "w");
                    if (file == NULL) {
                        perror("Failed to open file for overwriting");
                        format_result(&res, "Failed to overwrite file", "");
                        send(client_fd, &res, sizeof(res), 0);
                        log_add(client.name, "write", filename, "failed");
                        file_list[i].isModified = false;
                        return;
                    }
                    
                    fprintf(file, "%s", content);  // overwrite content
                    fclose(file);
                    format_result(&res, "File overwritten", "");
                }
                else { // additional
                    file = fopen(filepath, "a");
                    if (file == NULL) {
                        perror("Failed to open file for appending");
                        format_result(&res, "Failed to append content", "");
                        send(client_fd, &res, sizeof(res), 0);
                        log_add(client.name, "write", filename, "failed");
                        file_list[i].isModified = false;
                        return;
                    }
                    
                    fprintf(file, "%s", content);  // additional content
                    fclose(file);
                    format_result(&res, "Content appended", "");
                }

                // update file size
                struct stat st;
                if (stat(filepath, &st) == 0)  file_list[i].size = st.st_size;  
                else perror("Failed to get file size");

                // update last modified time
                time_t now = time(NULL);
                struct tm* tm_info = localtime(&now);
                strftime(file_list[i].last_modified, sizeof(file_list[i].last_modified), "%Y/%m/%d %H:%M", tm_info);

                send(client_fd, &res, sizeof(res), 0);
                log_add(client.name, "write", filename, "success");
            }
            else {
                Response res;
                format_result(&res, "Permission denied", "");
                send(client_fd, &res, sizeof(res), 0);
                log_add(client.name, "write", filename, "permission denied");
            }
            file_list[i].isModified = false;
            return;
        }
    }

    Response res;
    format_result(&res, "File not found", "");
    send(client_fd, &res, sizeof(res), 0);
}

// modify file permissions
void change_mode(int client_fd, User client, const char* filename, const char* permissions) {

    for (int i = 0; i < file_num; i++) {
        if (!strcmp(file_list[i].filename, filename)) { // found
            if (!strcmp(file_list[i].owner, client.name)) { // client is the owner
                strcpy(file_list[i].permissions, permissions);

                Response res;
                format_result(&res, "Permissions changed", "");
                send(client_fd, &res, sizeof(res), 0);
                log_add(client.name, "mode", filename, "permissions changed");
            } else {
                Response res;
                format_result(&res, "Permission denied", "");
                send(client_fd, &res, sizeof(res), 0);
                log_add(client.name, "mode", filename, "permission denied");
            }
            return;
        }
    }

    Response res;
    format_result(&res, "File not found", "");
    send(client_fd, &res, sizeof(res), 0);
}

// get file name fault tolerance
char* getFilename(char* command){
    char *token = strtok(command, " ");
        if (token && (!strcmp(token, "create") ||
                    !strcmp(token, "write") ||
                    !strcmp(token, "mode")
                    )
        )   token = strtok(NULL, " ");
    return token;
}

// handle client requests
void *client_handler(void *client_socket) {
    
    int client_fd = *(int *)client_socket;
    ClientRequest request;

    int read_size;

    // receive client request
    while ((read_size = recv(client_fd, &request, sizeof(ClientRequest), 0)) > 0) {

        User client     = request.user;
        char* command   = request.command;

        char filename[256], permissions[6], write_mode[2];

        // Commands from the client side
        if(!strlen(command)) {
            Response res;
            format_result(&res, "...", "");
            send(client_fd, &res, sizeof(res), 0);
        }
        else if (!strcmp(command, "ls")) {
            list_file(client_fd, client);
        }
        else if (sscanf(command, "create  %s %s", filename, permissions) == 2) {
            if(correctPermissionFormat(permissions))
                create_file(client_fd, client, getFilename(command), permissions); 
            else {
                Response res;
                format_result(&res, "Invalid command", "Permission format incorrect.(ex: rwrw--)");
                send(client_fd, &res, sizeof(res), 0);
            }
        }
        else if (sscanf(command, "read %s", filename) == 1) {
            read_file(client_fd, client, filename);
        }
        else if (sscanf(command, "write %s %s", filename, write_mode) == 2) {
            write_file(client_fd, client, getFilename(command), write_mode);
        }
        else if (sscanf(command, "mode %s %s", filename, permissions) == 2) {
            if(correctPermissionFormat(permissions))
                change_mode(client_fd, client, getFilename(command), permissions);
            else {
                Response res;
                format_result(&res, "Invalid command", "Permission format incorrect.(ex: rwrw--)");
                send(client_fd, &res, sizeof(res), 0);
            }
        }
        else {
            Response res;
            format_result(&res, "Invalid command", "Type \"help\" to view all the valid command.");
            send(client_fd, &res, sizeof(res), 0);
        }
    }

    close(client_fd);
    return NULL;
}

// Server management commands
void *admin_handler() {
    char command[256];

    while (server_running) {
        printf("admin> ");
        fflush(stdout);
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';  // Remove newline characters

        // server-side instructions
        if (!strcmp(command, "exit")) {     // exit
            printf("Shutting down server...\n");

            // establish a placeholder connection and break accept()
            int temp_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in temp_addr;
            temp_addr.sin_family = AF_INET;
            temp_addr.sin_port = htons(PORT);
            inet_pton(AF_INET, SERVER_ADDR, &temp_addr.sin_addr);
            connect(temp_fd, (struct sockaddr *)&temp_addr, sizeof(temp_addr));
            close(temp_fd);

            server_running = 0;  
            return NULL;
        }
        else if (!strcmp(command, "log")) {  // show the log file
            char buffer[CONTENT_SIZE];
            size_t numread;

            FILE *log = fopen("socket.log", "r");
            if (log == NULL) {
                printf("failed to open the file.\n\n");
                continue;
            }
            numread = fread(buffer, sizeof(char), CONTENT_SIZE, log);
            printf("read %lu bytes\n", numread);
            printf("=================================================================================\n");
            printf("%s\n", buffer);
            fclose(log);
        }
        else if (!strcmp(command, "list")) { // show the file list
            printf("Permission\tName                Owner        Group\tsize\tLast modified\n");
            printf("=================================================================================\n");
            
            for(int i = 0; i < file_num; i++)
                printf("%s\t\t%-17s   %-10s   %s\t%ld\t%s\n", 
                    file_list[i].permissions, 
                    file_list[i].filename, 
                    file_list[i].owner, 
                    file_list[i].group,
                    file_list[i].size,
                    file_list[i].last_modified
                );
            printf("\n");
        }
        else if (!strcmp(command, "help")) { // list the commands on server
            printf("\nThere're the commands on the server:\n");
            printf("================================================\n");
            printf(" exit:\tclose the server.\n");
            printf(" log:\tlist all the actions in log file.\n");
            printf(" list:\tlist all the files on the server.\n");
            printf("================================================\n\n");
        }
        else if (strlen(command))
            printf("Warning. Error command format. \nPlease input \"help\" to view all commands.\n\n");
    }

    return NULL;
}

int main(){

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    
    socklen_t client_num = sizeof(client_addr); // number of clients
    pthread_t thread_id, admin_thread;

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // setting server_addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect socket to server_addr
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // monitor connection
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d\n", PORT);
    printf("Input \"help\" to list the command in server.\n\n");

    // Start the thread for management instructions
    if (pthread_create(&admin_thread, NULL, admin_handler, NULL) != 0) {
        perror("Admin thread creation failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (server_running) {
        
        // client connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_num);

        // accept client connection
        if (client_fd >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

            if(server_running){
                printf("Client connected: %s:%d\n", client_ip, ntohs(client_addr.sin_port));
                printf("admin> ");
                fflush(stdout);
            }

            if (pthread_create(&thread_id, NULL, client_handler, (void *)&client_fd) < 0) {
                perror("Thread creation failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            } 
        }
        else{
            perror("Accept error");
            break;
        }
    }

    // Wait for the managed thread to end
    pthread_join(admin_thread, NULL);

    // close server socket
    close(server_fd);
    printf("Server shut down.\n");

    return 0;
}
