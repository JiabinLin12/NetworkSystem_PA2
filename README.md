# NetworkSystems_PA2

* This code implmented a webserver that support both get and post requests
* The connection will be closed after 10s not receiving packets
* Internal error like fetching error page will reach 500 internal error page
* This server is able to support multiple connections
* Usage:
  * navigate to the the assignment folder   
  * do "make" in terminal
    * make not only make the file, but also run the executable at port 8888 by default
  * Open browser and do "localhost:8888"
  * Play with the browser, the server will be close if no new request detected after 10 second          
