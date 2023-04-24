## Environment
- Language: C
- OS: 16.04 Ubuntu 64 Bit
## How to run
1. Run make all to complie c files.
```
make all
```
2. Open 4 terminals to run serverM(the main server), serverA(backend serverA), serverB(backend serverB), and client.
```
./serverM
```
```
./serverA
```
```
./serverB
```
```
./client
```
3. Enter usernames that you want to check availability into the client.(seperator: a single space)
    - Refer to a.txt and b.txt for stored usernames in the back servers.
    - If you enter usernames that do not exist in back servers, the main server will return a message that says they do not exist.
4. Register the meeting time with a format, "[%d,%d]".

## What each code file does:
    - serverM.c: It receives usernames the client entered and check if they are in either serverA or serverB. Then, it sends a reply to the client if any of usernames does not exist in either servers. If there are usernames existing in serverA or serverB, it sends only those names excluding not existing usernames to corresponding servers. Then, it receives the time intersections from serverA and serverB and find the intersection of both. Finally, it sends the final result(time intersections) to the client.
    - serverA.c: First of all, it sends usernames that it has to the main server. Then, it receives usernames from the main server and find time intersections between received users, and then sends the time intersection of them to the main server.
    - serverB.c: First of all, it sends usernames that it has to the main server. Then, it receives usernames from the main server and find time intersections between received users, and then sends the time intersection of them to the main server.
    - client.c: The client enters usernames to know their common availability and receives usernames that do not exist in back-servers and also receives intersections of time availability between entered usernames.
## Others
- Messages are concatenated and delimited by colon(';') while communicating except for the first message from the client to the main server, which contains usernames that the client entered. They are deliminated by a single space.
- As I experimented lots of input cases, sometimes, the final result from the main server is not received in the client. I set time out for 3 seconds. If the final result is not received within 3 seconds, the client should re-start the request. 
- Reused code:
    - I reused some codes for creating, binding, and connecting sockets from Beej's Guide.
