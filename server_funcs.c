// Implement the functions in this file to manipulate the server_t and client_t data that will ultimately
// be used by the server to fulfill its role.

# include "blather.h"

// Gets a pointer to the client_t struct at the given index. If the
// index is beyond n_clients, the behavior of the function is
// unspecified and may cause a program crash.
client_t *server_get_client(server_t *server, int idx) {
    check_fail(idx >= server->n_clients, 1, "idx out of bounds.\n");
    return &server->client[idx];
}

// Initializes and starts the server with the given name. A join fifo
// called "server_name.fifo" should be created. Removes any existing
// file of that name prior to creation. Opens the FIFO and stores its
// file descriptor in join_fd.
//
// ADVANCED: create the log file "server_name.log" and write the
// initial empty who_t contents to its beginning. Ensure that the
// log_fd is position for appending to the end of the file. Create the
// POSIX semaphore "/server_name.sem" and initialize it to 1 to
// control access to the who_t portion of the log.
//
// LOG Messages:
// log_printf("BEGIN: server_start()\n");              // at beginning of function
// log_printf("END: server_start()\n");                // at end of function
void server_start(server_t *server, char *server_name, int perms) {
    log_printf("BEGIN: server_start()\n");

    strcpy(server->server_name, server_name);
    char fifo_name[MAXNAME];
    strcpy(fifo_name, server_name);
    strcat(fifo_name, ".fifo"); // the full file name

    remove(fifo_name); // remove any existing file of that name
    mkfifo(fifo_name, perms); // create fifo file
    server->join_fd = open(fifo_name, O_RDWR); // open the FIFO and stores its file descriptor in join_fd

    check_fail(server->join_fd == -1, 1, "open fifo file %s fail.\n", fifo_name);
    // TODO Advanced


    log_printf("server_start: %s\n", server->server_name);
    log_printf("END: server_start()\n");
}

// Shut down the server. Close the join FIFO and unlink (remove) it so
// that no further clients can join. Send a BL_SHUTDOWN message to all
// clients and proceed to remove all clients in any order.
//
// ADVANCED: Close the log file. Close the log semaphore and unlink
// it.
//
// LOG Messages:
// log_printf("BEGIN: server_shutdown()\n");           // at beginning of function
// log_printf("END: server_shutdown()\n");             // at end of function
void server_shutdown(server_t *server) {
    log_printf("BEGIN: server_shutdown()\n");
    close(server->join_fd); // close the join FIFO
    remove(server->server_name); // remove FIFO

    mesg_t mesg;
    memset(&mesg, 0, sizeof(mesg_t));
    mesg.kind = BL_SHUTDOWN;
    server_broadcast(server, &mesg);

    for (int i = 0; i < server->n_clients; ++i) {
        server_remove_client(server, i);
    }
    // TODO Advanced
    close(server->log_fd);


    log_printf("server_shutdown: %s\n", server->server_name);
    log_printf("END: server_shutdown()\n");
}

// Adds a client to the server according to the parameter join which
// should have fields such as name filed in.  The client data is
// copied into the client[] array and file descriptors are opened for
// its to-server and to-client FIFOs. Initializes the data_ready field
// for the client to 0. Returns 0 on success and non-zero if the
// server as no space for clients (n_clients == MAXCLIENTS).
//
// LOG Messages:
// log_printf("BEGIN: server_add_client()\n");         // at beginning of function
// log_printf("END: server_add_client()\n");           // at end of function
int server_add_client(server_t *server, join_t *join) {
    log_printf("BEGIN: server_add_client()\n");
    if (server->n_clients >= MAXCLIENTS) {
        log_printf("END: server_add_client()\n");
        return -1; // return non-zero
    }

    client_t client;
    memset(&client, 0, sizeof(client_t));

    strcpy(client.name, join->name);
    strcpy(client.to_client_fname, join->to_client_fname);
    strcpy(client.to_server_fname, join->to_server_fname);
    client.last_contact_time = time(NULL); // timestamp of now

    client.to_client_fd = open(client.to_client_fname, O_RDWR);
    check_fail(client.to_client_fd == -1, 1, "open fifo file %s\n error", join->to_client_fname);
    client.to_server_fd = open(client.to_server_fname, O_RDWR);
    check_fail(client.to_server_fd == -1, 1, "open fifo file %s\n error", join->to_server_fname);

    // fill the message struct
    mesg_t join_mesg;
    memset(&join_mesg, 0, sizeof(mesg_t));
    join_mesg.kind = BL_JOINED;
    strcpy(join_mesg.name, client.name); // the name of client
    // sprintf(join_mesg.body, "%s join the server %s.", join->name, server->server_name);

    // add the client info to the server
    server->client[server->n_clients++] = client;
    server_broadcast(server, &join_mesg);

    log_printf("server_add_client: add %s to %s\n", join->name, server->server_name);
    log_printf("END: server_add_client()\n");
    return 0;
}

// Remove the given client likely due to its having departed or
// disconnected. Close fifos associated with the client and remove
// them.  Shift the remaining clients to lower indices of the client[]
// preserving their order in the array; decreases n_clients. Returns 0
// on success, 1 on failure.
int server_remove_client(server_t *server, int idx) {
    if (idx >= server->n_clients) {
        return -1;
    }

    client_t *client = server_get_client(server, idx); // get the client
    if (close(client->to_client_fd) == -1 || close(client->to_server_fd) == -1) {
        return -1;
    }
    remove(client->to_client_fname);
    remove(client->to_server_fname);

    // shift the remaining clients to lower indices of the client[]
    for (int i = idx; i < server->n_clients - 1; ++i) {
        server->client[i] = *server_get_client(server, i + 1);
    }
    server->n_clients -= 1;
    return 0;
}


// Send the given message to all clients connected to the server by
// writing it to the file descriptors associated with them.
//
// ADVANCED: Log the broadcast message unless it is a PING which
// should not be written to the log.
void server_broadcast(server_t *server, mesg_t *mesg) {
    // send the given message to all clients connected to the server
    for (int i = 0; i < server->n_clients; ++i) {
        long n_write = write(server_get_client(server, i)->to_client_fd,
                             mesg, sizeof(mesg_t));
        check_fail(n_write == -1, 1, "write to fd %d error.\n", server_get_client(server, i)->to_client_fd);
    }
    log_printf("server_broadcast: %s\n", mesg->body);
    // TODO Advanced

}

// Checks all sources of data for the server to determine if any are
// ready for reading. Sets the servers join_ready flag and the
// data_ready flags of each of client if data is ready for them.
// Makes use of the poll() system call to efficiently determine which
// sources are ready.
//
// NOTE: the poll() system call will return -1 if it is interrupted by
// the process receiving a signal. This is expected to initiate server
// shutdown and is handled by returning immediately from this function.
//
// LOG Messages:
// log_printf("BEGIN: server_check_sources()\n");             // at beginning of function
// log_printf("poll()'ing to check %d input sources\n",...);  // prior to poll() call
// log_printf("poll() completed with return value %d\n",...); // after poll() call
// log_printf("poll() interrupted by a signal\n");            // if poll interrupted by a signal
// log_printf("join_ready = %d\n",...);                       // whether join queue has data
// log_printf("client %d '%s' data_ready = %d\n",...)         // whether client has data ready
// log_printf("END: server_check_sources()\n");               // at end of function
void server_check_sources(server_t *server) {
    log_printf("BEGIN: server_check_sources()\n");

    struct pollfd poll_fds[2 + MAXCLIENTS];
    memset(poll_fds, 0, sizeof(poll_fds));
    poll_fds[0].fd = server->join_fd;
    poll_fds[0].events |= POLLIN;
    poll_fds[1].fd = server->log_fd; //
    poll_fds[1].events |= POLLIN;
    for (int i = 0; i < server->n_clients; ++i) {
        poll_fds[i + 2].fd = server->client[i + 2].to_server_fd;
        poll_fds[i + 2].events |= POLLIN;
    }

    log_printf("poll()'ing to check %d input sources\n");
    int num = poll(poll_fds, POLLIN, -1);
    log_printf("poll() completed with return value %d\n", num);
    if (num == -1) {
        log_printf("poll() interrupted by a signal\n");
        return;
    }

    // check the join_fd
    if (POLLIN & poll_fds[0].revents) {
        log_printf("join_ready = %d\n", 1);
        server->join_ready = 1;
    } else {
        log_printf("join_ready = %d\n", 0);
    }

    // check all the clients fd
    for (int i = 0; i < server->n_clients; i++) {
        if (POLLIN & poll_fds[i + 2].revents) {
            log_printf("client %d '%s' data_ready = %d\n", i, server_get_client(server, i).name, 1);
            server_get_client(server, i).data_ready = 1;
        } else {
            log_printf("client %d '%s' data_ready = %d\n", i, server_get_client(server, i).name, 0);
        }
    }

    log_printf("END: server_check_sources()\n");
}

// Return the join_ready flag from the server which indicates whether
// a call to server_handle_join() is safe.
int server_join_ready(server_t *server) {
    return server->join_ready;
}

// Call this function only if server_join_ready() returns true. Read a
// join request and add the new client to the server. After finishing,
// set the servers join_ready flag to 0.
//
// LOG Messages:
// log_printf("BEGIN: server_handle_join()\n");               // at beginning of function
// log_printf("join request for new client '%s'\n",...);      // reports name of new client
// log_printf("END: server_handle_join()\n");                 // at end of function
void server_handle_join(server_t *server) {
    log_printf("BEGIN: server_handle_join()\n");
    join_t join;
    memset(&join, 0, sizeof(join_t));
    long n_read = read(server->join_fd, &join, sizeof(join_t));
    check_fail(n_read == -1, 1, "read fd %d error.\n", server->join_fd);
    log_printf("join request for new client '%s'\n", join.name);

    server->join_ready = 0;
    log_printf("END: server_handle_join()\n");
}


// Return the data_ready field of the given client which indicates
// whether the client has data ready to be read from it.
int server_client_ready(server_t *server, int idx) {
    check_fail(idx >= server->n_clients, 1, "idx out of bounds.\n");
    return server->client[idx].data_ready;
}


// Process a message from the specified client. This function should
// only be called if server_client_ready() returns true. Read a
// message from to_server_fd and analyze the message kind. Departure
// and Message types should be broadcast to all other clients.  Ping
// responses should only change the last_contact_time below. Behavior
// for other message types is not specified. Clear the client's
// data_ready flag so it has value 0.
//
// ADVANCED: Update the last_contact_time of the client to the current
// server time_sec.
//
// LOG Messages:
// log_printf("BEGIN: server_handle_client()\n");           // at beginning of function
// log_printf("client %d '%s' DEPARTED\n",                  // indicates client departed
// log_printf("client %d '%s' MESSAGE '%s'\n",              // indicates client message
// log_printf("END: server_handle_client()\n");             // at end of function
void server_handle_client(server_t *server, int idx) {
    log_printf("BEGIN: server_handle_client()\n");
    mesg_t mesg;
    memset(&mesg, 0, sizeof(mesg_t));
    int n_read = read(server_get_client(server, idx)->to_server_fd, &mesg, sizeof(mesg_t));
    check_fail(n_read == -1, 1, "read fd %d error.\n", server_get_client(server, idx)->to_server_fd);
    server_get_client(server, idx)->data_ready = 0;
    server_get_client(server, idx)->last_contact_time = time(NULL);

    switch (mesg.kind) {
        BL_DEPARTED:
            server_remove_client(server.idx);
            server_broadcast(server, mesg);
            break;
        BL_MESG:
            server_broadcast(server, mesg);
            break;
        BL_DISCONNECTED: // TODO Advanced
            break;
        BL_PING:
            break;
        BL_SHUTDOWN: // do nothing here
            break;
        default:
            break;
    }

    log_printf("END: server_handle_client()\n");
}


// TODO Advanced
// ADVANCED: Increment the time for the server
void server_tick(server_t *server) {

}

// ADVANCED: Ping all clients in the server by broadcasting a ping.
void server_ping_clients(server_t *server) {

}

// ADVANCED: Check all clients to see if they have contacted the
// server recently. Any client with a last_contact_time field equal to
// or greater than the parameter disconnect_secs should be
// removed. Broadcast that the client was disconnected to remaining
// clients.  Process clients from lowest to highest and take care of
// loop indexing as clients may be removed during the loop
// necessitating index adjustments.
void server_remove_disconnected(server_t *server, int disconnect_secs) {

}

// ADVANCED: Write the current set of clients logged into the server
// to the BEGINNING the log_fd. Ensure that the write is protected by
// locking the semaphore associated with the log file. Since it may
// take some time to complete this operation (acquire semaphore then
// write) it should likely be done in its own thread to preven the
// main server operations from stalling.  For threaded I/O, consider
// using the pwrite() function to write to a specific location in an
// open file descriptor which will not alter the position of log_fd so
// that appends continue to write to the end of the file.
void server_write_who(server_t *server) {

}

// ADVANCED: Write the given message to the end of log file associated
// with the server.
void server_log_message(server_t *server, mesg_t *mesg) {

}
