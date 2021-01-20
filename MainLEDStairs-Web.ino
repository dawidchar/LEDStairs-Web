#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h> // Include the Wi-Fi library
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>

ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80
WebSocketsServer webSocket(81); // create a websocket server on port 81

File fsUploadFile; // a File variable to temporarily store the received file

const char *ssid = ""; // The SSID (name) of the Wi-Fi network you want to connect to
const char *password = "";
const char *OTAName = "LEDDown"; // A name and a password for the OTA service
const char *OTAPassword = "password";
const char *mdnsName = "LEDDown"; // Domain name for the mDNS responder

#define pinLED 4
#define LEDcount 481
#define numSteps 13
#define pinSensorBottom 12
#define pinSensorTop 14
int LEDsPerStair = (LEDcount / numSteps);

Adafruit_NeoPixel strip = Adafruit_NeoPixel(LEDcount, pinLED, NEO_GRB + NEO_KHZ800);

/*
  Modes:
  0-Normal
  1-Rainbow Per Stair
  2-Candy Cane
  3-Still
  4-Rainbow All Stairs
  5-Still with Flashing
*/

unsigned long timeOut;
unsigned long timeOutTime = 5000;
float baseColor[3] = {0, 56, 190};
float timingOffset[6] = {25, 50, 150, 50, 50, 250};
float timingOffsetInterval = 10;
float timingOffsetMax = 500;
int fadeSmoothness = 10;

bool onOff = true;
//float brightnessMax = 0.8;
float brightness = 0.2;
//float brightnessInterval = 0.1;
int LEDmode = 0;
int sensorValTop = LOW;
int sensorValBottom = LOW;
int directionVal = 0;
bool exitflag = false;

//Sidelights
bool sideLights = true;
bool sideLightsRestricted = false;
float sideLightColor[3] = {0, 0, 255};
float sideLightBreathe = 0.9;
float sideLightChange = 0.01;

void setup()
{
  Serial.begin(115200); // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println('\n');

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(pinLED, OUTPUT);
  pinMode(pinSensorBottom, INPUT_PULLUP);
  pinMode(pinSensorTop, INPUT_PULLUP);

  digitalWrite(LED_BUILTIN, LOW);
  beginWIFI();
  beginStrip();

  startOTA();
  startSPIFFS();
  startWebSocket();
  startMDNS();
  startServer();
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("All Started");
}

void beginWIFI()
{
  WiFi.begin(ssid, password); // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED)
  { // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print(++i);
    Serial.print(' ');
  }

  Serial.println('\n');
  Serial.println("Connection established!");
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
}

void beginStrip()
{
  strip.begin();
  strip.show();
}

void startOTA()
{ // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\r\n OTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");
}

void startSPIFFS()
{ // Start the SPIFFS and list all contents
  SPIFFS.begin(); // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    { // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}

void startWebSocket()
{ // Start a WebSocket server
  webSocket.begin();                 // start the websocket server
  webSocket.onEvent(webSocketEvent); // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");
}

void startMDNS()
{ // Start the mDNS responder
  MDNS.begin(mdnsName); // start the multicast domain name server
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void startServer()
{ // Start a HTTP server with a file read handler and an upload handler
  server.on("/edit.html", HTTP_POST, []() { // If a POST request is sent to the /edit.html address,
    server.send(200, "text/plain", "");
  },
  handleFileUpload); // go to 'handleFileUpload'

  server.onNotFound(handleNotFound); // if someone requests any other file or page, go to function 'handleNotFound'

  server.begin(); // start the HTTP server
  Serial.println("HTTP server started.");
}

void handleNotFound()
{ // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri()))
  { // check if the file exists in the flash memory (SPIFFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path)
{ // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/"))
    path += "index.html";                    // If a folder is requested, send the index file
  String contentType = getContentType(path); // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                      // If there's a compressed version available
      path += ".gz";                                    // Use the compressed verion
    File file = SPIFFS.open(path, "r");                 // Open the file
    size_t sent = server.streamFile(file, contentType); // Send it to the client
    file.close();                                       // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path); // If the file doesn't exist, return false
  return false;
}

void handleFileUpload()
{ // upload a new file to the SPIFFS
  HTTPUpload &upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START)
  {
    path = upload.filename;
    if (!path.startsWith("/"))
      path = "/" + path;
    if (!path.endsWith(".gz"))
    { // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz"; // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))    // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: ");
    Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w"); // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    { // If the file was successfully created
      fsUploadFile.close(); // Close the file again
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html"); // Redirect the client to the success page
      server.send(303);
    }
    else
    {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

String formatBytes(size_t bytes)
{ // convert sizes in bytes to KB and MB
  if (bytes < 1024)
  {
    return String(bytes) + "B";
  }
  else if (bytes < (1024 * 1024))
  {
    return String(bytes / 1024.0) + "KB";
  }
  else if (bytes < (1024 * 1024 * 1024))
  {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename)
{ // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  return "text/plain";
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{ // When a WebSocket message is received

  switch (type)
  {
    case WStype_DISCONNECTED: // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      { // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        char txtBuffer [50];
      Serial.printf("T%u|%f", LEDmode, timingOffset[LEDmode]);
      webSocket.sendTXT(num, txtBuffer);
      }
      break;
    case WStype_TEXT: // if new text data is received
      Serial.printf("[%u] get Text: %s\n", num, payload);

      if (payload[0] == 'C')
      { // we get RGB data
        uint32_t rgb = (uint32_t)strtol((const char *)&payload[2], NULL, 16); // decode rgb data
        int r = ((rgb >> 20) & 0x3FF);                                        // 10 bits per color, so R: bits 20-29
        int g = ((rgb >> 10) & 0x3FF);                                        // G: bits 10-19
        int b = rgb & 0x3FF;
        Serial.print("R: ");
        Serial.println(r);
        Serial.print("G: ");
        Serial.println(g);
        Serial.print("B: ");
        Serial.println(b);
        // B: bits  0-9
        if (payload[1] == 'M')
        {
          baseColor[0] = r;
          baseColor[1] = g;
          baseColor[2] = b;
        }
        else if (payload[1] == 'S')
        {
          sideLightColor[0] = r;
          sideLightColor[1] = g;
          sideLightColor[2] = b;
        }
      }
      else if (payload[0] == 'P')
      {
        if (payload[1] == 'M')
        {

          Serial.print("onOFF ");
          Serial.println(onOff);
          Serial.print("Payload[2]");
          Serial.println(payload[2]);
          onOff = (uint32_t)strtol((const char *)&payload[2], NULL, 10);
          Serial.print("onOFF ");
          Serial.println(onOff);

          if (!onOff)
            setAllColor(new float[3] {0, 0, 0}, 0);
        }
        else if (payload[1] == 'S')
        {

          Serial.print("sideLights ");
          Serial.println(sideLights);
          Serial.print("Payload[2] ");
          Serial.println(payload[2]);
          sideLights = (uint32_t)strtol((const char *)&payload[2], NULL, 10);
          Serial.print("sideLights ");
          Serial.println(sideLights);
          if (!onOff && LEDmode == 0)
            setAllColor(new float[3] {0, 0, 0}, 0);

        }
      }
      else if (payload[0] == 'M')
      {
        LEDmode = (uint32_t)strtol((const char *)&payload[1], NULL, 10);
        Serial.println((uint32_t)strtol((const char *)&payload[1], NULL, 10));
        Serial.println(LEDmode);
        Serial.printf("T%u|%f", LEDmode, timingOffset[LEDmode]);
        //String txtBuffer;
        char txtBuffer [50];
        //      txtBuffer += "T";
        //      txtBuffer += LEDmode;
        //      txtBuffer += "|";
        //      txtBuffer += timingOffset[LEDmode];

        sprintf(txtBuffer, "T%u%f", LEDmode, timingOffset[LEDmode]);
        webSocket.sendTXT(num, txtBuffer);
        exitflag = true;
        if (LEDmode == 0) resetNormal();
        setAllColor(new float[3] {0, 0, 0}, 0);
      }
      else if (payload[0] == 'T')
      {
        Serial.println((uint32_t)strtol((const char *)&payload[1], NULL, 16));
        timingOffset[LEDmode] = (uint32_t)strtol((const char *)&payload[1], NULL, 10);
      }
      else if (payload[0] == 'B') {

        brightness = ((float)strtol((const char *)&payload[1], NULL, 10) / 100);
        Serial.print("brightness ");
        Serial.println(brightness);
      }
      else if (payload[0] == 'R') {
ESP.reset();
      }
      if (LEDmode == 3) setAllColor(baseColor, 0);
      break;
  }
}

void resetNormal(){
timeOut = 0;
directionVal = 0;
sensorValTop = LOW;
sensorValBottom = LOW;
}

void loop()
{
   sensorValTop = digitalRead(pinSensorTop);
   sensorValBottom = digitalRead(pinSensorBottom);

  webSocket.loop();      // constantly check for websocket events
  server.handleClient(); // run the server
  ArduinoOTA.handle();   // listen for OTA events

  //Serial.println(LEDmode);
  if (onOff) runMode();
}

void runMode()
{
  exitflag = false;
  switch (LEDmode)
  {
    case 0:
      runNormal();
      runSideLights();
      break;
    case 1:
      runRainbowPerStep(timingOffset[LEDmode]);
      break;
    case 2:
      runCandyCane(timingOffset[LEDmode]);
      break;
    case 3:
      //setAllColor(baseColor, 0);
      break;
    case 4:
      //Rainbow All
      break;
    case 5:
      runFlashStill(timingOffset[LEDmode]);
      break;
  }
}

void runNormal()
{
  if (sensorValTop == HIGH && directionVal != 1)
  {
    directionVal = 2;
    sideLightsRestricted = true;
    if (timeOut < millis())
    {
      Serial.println("detected top");
      colourWipeDown(baseColor, timingOffset[0]);
    }
    timeOut = millis() + timeOutTime;
  }

  if (sensorValBottom == HIGH && directionVal != 2)
  {
    directionVal = 1;
    sideLightsRestricted = true;
    if (timeOut < millis())
    {
      Serial.println("detected bottom");
      colourWipeUp(baseColor, timingOffset[0]);
    }
    timeOut = millis() + timeOutTime;
  }
  if (directionVal != 0 && timeOut < millis())
  { //switch off LED's in the direction of travel.
    Serial.println("Clearing");
    if (directionVal == 1)
    {
      colourWipeDown(new float[3] {0, 0, 0}, 50); // Off
    }
    if (directionVal == 2)
    {
      colourWipeUp(new float[3] {0, 0, 0}, 50); // Off
    }
    directionVal = 0;
    sideLightsRestricted = false;
  }
}

void runSideLights()
{
  if (sideLights && !sideLightsRestricted && timeOut + 100 < millis())
  {
    sideLightBreathe = sideLightBreathe + sideLightChange;
    if (sideLightBreathe < 0)
      sideLightBreathe = 0;
    if (sideLightBreathe > 1)
      sideLightBreathe = 1;
    for (int i = 0; i < numSteps; i++)
    {
      strip.setPixelColor(i * LEDsPerStair, strip.Color(sideLightColor[0] * sideLightBreathe * brightness, sideLightColor[1] * sideLightBreathe * brightness, sideLightColor[2] * sideLightBreathe * brightness));
      strip.setPixelColor(i * LEDsPerStair + (LEDsPerStair - 1), strip.Color(sideLightColor[0] * sideLightBreathe * brightness, sideLightColor[1] * sideLightBreathe * brightness, sideLightColor[2] * sideLightBreathe * brightness));
    }
    strip.show();
    IRwait(20);

    if (sideLightBreathe >= 1 || sideLightBreathe <= 0)
      sideLightChange = -sideLightChange; // breathe the LED from 0 = off to 100 = fairly bright
    if (sideLightBreathe >= 1 || sideLightBreathe <= 0)
      IRwait(100);
  }
}

void colourWipeUp(float inColor[], uint16_t wait)
{
  for (uint16_t j = 0; j < numSteps; j++)
  {
    int start = LEDsPerStair * j;
    Serial.println(j);
    for (float k = 1; k <= fadeSmoothness; k++)
    {
      for (uint16_t i = start; i < start + (LEDsPerStair); i++)
      {
        // Serial.println(r* (k/fadeSmoothness));
        strip.setPixelColor(i, strip.Color(inColor[0] * (k / fadeSmoothness) * brightness, inColor[1] * (k / fadeSmoothness) * brightness, inColor[2] * (k / fadeSmoothness) * brightness));
      }
      strip.show();
      IRwait(wait);
       if (exitflag)
      return;
    }
  }
}

void colourWipeDown(float inColor[], uint16_t wait)
{

  for (int j = numSteps - 1; j > -1; j--)
  {
    int start = LEDsPerStair * j;
    Serial.println(j);
    for (float k = 1; k <= fadeSmoothness; k++)
    {
      for (uint16_t i = start; i < start + (LEDsPerStair); i++)
      {
        strip.setPixelColor(i, strip.Color(inColor[0] * (k / fadeSmoothness) * brightness, inColor[1] * (k / fadeSmoothness) * brightness, inColor[2] * (k / fadeSmoothness) * brightness));
      }
      strip.show();
      IRwait(wait);
       if (exitflag)
      return;
    }
  }
}

void runCandyCane(int wait)
{
  int L;
  int width = 5;

  for (int j = 0; j < (width * 2); j++)
  {
    for (int i = 0; i < strip.numPixels(); i++)
    {
      L = strip.numPixels() - i - 1;
      if (((i + j) % (width * 2)) < width)
        strip.setPixelColor(L, 255 * brightness, 0, 0);
      else
        strip.setPixelColor(L, 255 * brightness, 255 * brightness, 255 * brightness);
    };
    strip.show();
    IRwait(wait);
    if (exitflag)
      return;
  };
}

void runFlashStill(int wait)
{
  int i, j;
  float V;
  for (j = 0; j < strip.numPixels(); j++)
  {
    V = random(100) * brightness;
    strip.setPixelColor(j, baseColor[0] * V, baseColor[1] * V, baseColor[2] * V);
  }
  strip.show();
  IRwait(wait);
  if (exitflag)
    return;
}

void runRainbowPerStep(int wait)
{
  uint16_t i, j;
  for (j = 0; j < 256; j++)
  { //cycles of all colors on wheel
    for (i = 0; i < strip.numPixels(); i++)
    {
      strip.setPixelColor(strip.numPixels() - i - 1, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    IRwait(wait);
    if (exitflag)
      return;
  }
}

void setAllColor(float inColor[], uint16_t wait) {
  Serial.print("Set All: ");
  Serial.print(inColor[0]);
  Serial.print(", ");
  Serial.print(inColor[1]);
  Serial.print(", ");
  Serial.println(inColor[2]);
  for (int l = 0; l < strip.numPixels(); l++)
  {
    strip.setPixelColor(l, strip.Color(inColor[0] * brightness, inColor[1] * brightness, inColor[2] * brightness));
  }
  strip.show();
  delay(wait);
}

void IRwait(int wait)
{
  int delaytimeout = millis() + wait;
  while (delaytimeout > millis())
  {
    webSocket.loop();      // constantly check for websocket events
    server.handleClient(); // run the server
    delay(1);
  }
}

uint32_t Wheel(byte WheelPos)
{
  if (WheelPos < 85)
  {
    return strip.Color((WheelPos * 3) * brightness, (255 - WheelPos * 3) * brightness, 0);
  }
  else if (WheelPos < 170)
  {
    WheelPos -= 85;
    return strip.Color((255 - WheelPos * 3) * brightness, 0, (WheelPos * 3) * brightness);
  }
  else
  {
    WheelPos -= 170;
    return strip.Color(0, (WheelPos * 3) * brightness, (255 - WheelPos * 3) * brightness);
  }
}
