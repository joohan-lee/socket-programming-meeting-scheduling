1. Full Name: Joohan Lee
2. Student ID: 7711577119
3. I have implemented all phase 1 through 4 mentioned in the description. When the user entered only invalid usernames that are not in either serverA or serverB, the main server only replies to the client with a message and does nothing. Then the user can start a new request.
4. What each code file does:
    - serverM.c: It receives usernames the client entered and check if they are in either serverA or serverB. Then, it sends a reply to the client if any of usernames does not exist in either servers. If there are usernames existing in serverA or serverB, it sends only those names excluding not existing usernames to corresponding servers. Then, it receives the time intersections from serverA and serverB and find the intersection of both. Finally, it sends the final result(time intersections) to the client.
    - serverA.c: First of all, it sends usernames that it has to the main server. Then, it receives usernames from the main server and find time intersections between received users, and then sends the time intersection of them to the main server.
    - serverB.c: First of all, it sends usernames that it has to the main server. Then, it receives usernames from the main server and find time intersections between received users, and then sends the time intersection of them to the main server.
    - client.c: The client enters usernames to know their common availability and receives usernames that do not exist in back-servers and also receives intersections of time availability between entered usernames.
5. Messages are concatenated and delimited by colon(';') while communicating except for the first message from the client to the main server, which contains usernames that the client entered. They are deliminated by a single space.
6. As I experimented lots of input cases, sometimes, the final result from the main server is not received in the client. I set time out for 3 seconds. If the final result is not received within 3 seconds, the client should re-start the request. (**Please terminate all terminals and re-run all the servers and clients if timed out happens.**)
7. Reused code:
    - I reused some codes for creating, binding, and connecting sockets from Beej's Guide.
    - Any other codes without comments of their origin are written by me.