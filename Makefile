server: server.cpp
	g++ server.cpp -o server

subscriber: client.cpp
	g++ client.cpp -o subscriber

clean:
	rm -f server subscriber