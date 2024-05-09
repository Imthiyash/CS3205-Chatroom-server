On Server Side:
We're printing the user name and the socket-Id with which the client has 
joined the chatroom in server console.

On Client Side:
Everything is implemented as required in the question.

Note: Case when the client exits via ctrl-c is also handled.

When a client tries to connect after max connections have already been 
established the client is kept waiting till one of the connected client
disconnects from the server.