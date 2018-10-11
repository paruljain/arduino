#include <ESP8266WiFi.h>
 
const char* ssid = "********";
const char* password = "********";
 
const int relayPin = D1;
const long pulseInterval = 500; //relay pulse 0.5s

WiFiServer server(80);
IPAddress ip(192, 168, 1, 22); // where xx is the desired IP Address
IPAddress gateway(192, 168, 1, 1); // set gateway to match your network
IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network 

void setup() {
  Serial.begin(9600);
  delay(10);
 
  pinMode(relayPin, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH);
  digitalWrite(relayPin, LOW);
  
  Serial.print(F("Setting static ip to : "));
  Serial.println(ip);
 
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.config(ip, gateway, subnet); 
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
 
  // Start the server
  server.begin();
  Serial.println("Server started");
 
  // Print the IP address
  Serial.print("Use this URL : ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
 
}
 
void loop() {
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
 
  // Wait until the client sends some data
  Serial.println("new client");
  //while(!client.available()){
  //  delay(1);
  //}
  
  // Read the first line of the request
  String request = client.readStringUntil('\r');
  Serial.println(request);
  client.flush();
 
  // API
  if (request.indexOf("/relay?op=pulse") != -1) {
    digitalWrite(BUILTIN_LED, LOW);
    digitalWrite(relayPin, HIGH);
    delay(pulseInterval);
    digitalWrite(BUILTIN_LED, HIGH);
    digitalWrite(relayPin, LOW);
  } 
 
  // Return the response
  client.println("HTTP/1.1 200 OK");
  client.println("Connection: close");
  delay(1);
  Serial.println("Client disconnected");
  Serial.println("");
}
