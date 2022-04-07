# httpserver with multi-threading and redundancy

This program is a multi-threaded HTTP server which will read and respond to standard GET and PUT requests. It also has the option to add redundancy, which will create three copies of each file in separate folders.

## Usage

`./httpserver <serveraddress> [port] [-N number-of-threads] [-r]`

Arguments:
- `serveraddress`: 
    - Specifies the hostname or IP address (i.e. localhost or 127.0.0.1)
    - Required
- `port`:
    - Specifies the server's port number
    - Optional. Default: 80
- `-N`:
    - The number of threads the server will use
    - Optional. Default: 4
- `-r`:
    - Specifies whether or not the server should use redundancy
    - Optional. Default: no redundancy

### Example usage:
`./httpserver localhost 8080 -N 6 -r`

Starts the server at localhost:8080 with 6 threads and redundancy on.

`./httpserver 127.0.0.1`

Starts the server at 127.0.0.1:80 with 4 threads and redundancy off.

## Limitations

The server will not respond or accept requests other than GET or PUT. i.e. HEAD, POST, DELETE, etc.
