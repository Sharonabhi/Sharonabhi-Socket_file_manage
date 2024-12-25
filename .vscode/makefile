# Define the compiler and compilation parameters
CC = gcc

# CFLAGS: Set compilation options
# -Wall: Enable all warnings.
# -Wextra: Enable additional warnings.
# -pthread: Support multithreaded programming.
CFLAGS = -Wall -Wextra -pthread

FILES_DIR = files

# Target file names
SERVER = server
CLIENT = client
LOG = socket.log

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Compile all targets
all: $(SERVER) $(CLIENT)

# Compile the server
$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)
$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

# Clean up generated files
clean:
	rm -f $(SERVER) $(CLIENT) $(LOG)
	rm -rf $(FILES_DIR)
