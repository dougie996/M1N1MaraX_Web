
/* Copyright (C) 2024 Ralf Grafe
This file is partly based on MaraX-Shot-Monitor <https://github.com/Anlieger/MaraX-Shot-Monitor>.

M1N1MaraX_Web is a free software you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

M1N1MaraX_Web is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

//Includes
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SoftwareSerial.h>
#include <Timer.h>
#include <Event.h>
#include "bitmaps.h"
#include <ESP8266WiFi.h>
#include <ArduinoHttpClient.h>
#include "secrets.h"


//Definessss
#define SCREEN_WIDTH 128  //Width in px
#define SCREEN_HEIGHT 64  // Height in px
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C  // or 0x3D Check datasheet or Oled Display
#define BUFFER_SIZE 32

#define D5 (14)             // D5 is Rx Pin
#define D6 (12)             // D6 is Tx Pin
#define INVERSE_LOGIC true  // Use inverse logic for MaraX V2
#define D7 (13)

#define DEBUG false
// #define PUSH_MESSAGE // Aktivieren um Push Message anstelle von WebServer zu verwenden

//Internals
int state = LOW;
char on = LOW;
char off = HIGH;

long timerStartMillis = 0;
long timerStopMillis = 0;
long timerDisplayOffMillis = 0;
int timerCount = 0;
bool timerStarted = false;
bool displayOn = false;

bool initialPushSent = false;
String pushTitle = "Lelit%20Mara%20X";
String pushMessage = "I%20am%20hot%20for%20you!";
String pushIcon = "62";

int prevTimerCount = 0;
long serialTimeout = 0;
char buffer[BUFFER_SIZE];
int bufferIndex = 0;
int isMaraOff = 0;
long lastToggleTime = 0;
int HeatDisplayToggle = 0;
int pumpInValue = 0;
const int Sim = 0;
int tt = 8;

//Secrets from secrets.h
//Will not be pushed into the repo you have to create the file by yourself
String ssid = WLAN_SSID;
String pass = WLAN_PASS;
String apiKey = API_KEY;
String pushServiceHost = "pushsafer.com";
int pushServicePort = 80;


//Mara Data
String maraData[7];

//Instances
#ifdef PUSH_MESSAGE   // Either use WebServer or WiFi Client for sending PushMessage
WiFiClient wifi;
HttpClient client = HttpClient(wifi, pushServiceHost, pushServicePort);
#else
WiFiServer server(80);
#endif

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SoftwareSerial mySerial(D5, D6, INVERSE_LOGIC);  // Rx, Tx, Inverse_Logic
Timer t;

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  display.display();
  Serial.begin(9600);
  mySerial.begin(9600);
  //mySerial.write(0x11);  // this is XON Flow Control Chr ... do not use. 

  pinMode(LED_BUILTIN, OUTPUT);
  //  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  //
  #ifndef PUSH_MESSAGE // Setup Webserver
    Serial.println("Connecting");
    WiFi.begin(ssid, pass);
    WiFi.hostname("MaraX");
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println(".");
    }
    Serial.println("WiFi connected");
    server.begin();  // Starts the Server
    Serial.println("Server started");

    Serial.print("IP Address of network: ");  // Prints IP address on Serial Monitor
    Serial.println(WiFi.localIP());
    Serial.print("Copy and paste the following URL: https://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  #endif

  t.every(1000, updateView);
  t.every(500, publishWebpage);
}

void getMaraData() {
  /*
    Example Data: C1.06,116,124,093,0840,1,0\n every ~400-500ms
    Length: 26
    [Pos] [Data] [Describtion]
    0)      C     Coffee Mode (C) or SteamMode (V) // "+" in case of Mara X V2
    -       1.22  Software Version
    1)      116   current steam temperature (Celsisus)
    2)      124   target steam temperature (Celsisus)
    3)      093   current hx temperature (Celsisus)
    4)      0840  countdown for 'boost-mode'
    5)      1     heating element on or off
    6)      0     pump on or off
  */



  while (mySerial.available()) {    // true, as long there are chrs in the Rx Buffer
    isMaraOff = 0;                  // Mara is not off
    serialTimeout = millis();       // save current time
    char rcv = mySerial.read();     // read next chr
    if (rcv != '\n')                // test if not CR
      buffer[bufferIndex++] = rcv;  // add to buffer and increase counter
    else {                          // CR received = EOM
      bufferIndex = 0;              // set buffer index to 0
      Serial.println(buffer);       // print buffer on serial monitor
      char* ptr = strtok(buffer, ",");
      int idx = 0;
      while (ptr != NULL) {
        maraData[idx++] = String(ptr);
        ptr = strtok(NULL, ",");
      }
    }
  }


#ifdef PUSH_MESSAGE

  //Check if machine is ready
  if (maraData[1].toInt() == maraData[2].toInt() && maraData[3].toInt() > 90 && initialPushSent == false) {
    sendPushSaferMessage();
    initialPushSent = true;
  }
#endif


  if (millis() - serialTimeout > 999) {  // are there 1000ms passed after last chr rexeived?
    isMaraOff = 1;                       // Mara is off 
    if (DEBUG == true) {
      Serial.println("No Rx");  // Inserted for debugging
    }
    serialTimeout = millis();
    mySerial.write(0x11);
  }
}

void detectChanges() {
//
  if (maraData[6].toInt() == 1) {
    if (!timerStarted) {
      timerStartMillis = millis();
      timerStarted = true;
      displayOn = true;
      Serial.println("Start pump");
    }
  }
  if (maraData[6].toInt() == 0) {
   if (timerStarted) {
      if (timerStopMillis == 0) {
        timerStopMillis = millis();
      }
      if (millis() - timerStopMillis > 500) {
        timerStarted = false;
        timerStopMillis = 0;
        timerDisplayOffMillis = millis();
        display.invertDisplay(false);
        Serial.println("Stop pump");
        tt = 8;

        delay(4000);
      }
    }
  } else {
    timerStopMillis = 0;
  }
}

String getTimer() {
  char outMin[2];
  if (timerStarted) {
    timerCount = (millis() - timerStartMillis) / 1000;
    if (timerCount > 4) {
      prevTimerCount = timerCount;
    }
  } else {
    timerCount = prevTimerCount;
  }
  if (timerCount > 99) {
    return "99";
  }
  sprintf(outMin, "%02u", timerCount);
  return outMin;
}

#ifdef PUSH_MESSAGE // Not used when WebServer is active

void sendPushSaferMessage() {
  String params = "k=" + String(API_KEY) + "&t=" + pushTitle + "&m=" + pushMessage + "&i=" + pushIcon + "&d=a";
  Serial.print("POST Data: ");
  Serial.println(params);
  client.beginRequest();
  client.post("/api");
  client.sendHeader("Content-Type", "application/x-www-form-urlencoded");
  client.sendHeader("Content-Length", params.length());
  client.beginBody();
  client.print(params);
  client.endRequest();

  // Read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
}
#endif

void updateView() {
  if (DEBUG == true) {
    Serial.println(serialTimeout);  // Inserted for debugging
    Serial.println("Update View");  // Inserted for debugging
  }
  display.clearDisplay();
  display.setTextColor(WHITE);

  if (isMaraOff == 1) {
    if (DEBUG == true) {
      Serial.println("Mara is off");  // Inserted for debugging
    }
    display.setCursor(30, 6);
    display.setTextSize(2);
    display.print("MARA X");
    display.setCursor(30, 28);
    display.setTextSize(4);
    display.print("OFF");
  } else {
    if (timerStarted) {
      // draw the timer on the right
      display.fillRect(60, 9, 63, 55, BLACK);
      display.setTextSize(5);
      display.setCursor(68, 20);
      display.print(getTimer());

      if (timerCount >= 20 && timerCount <= 24) {
        display.setTextSize(5);
        display.setCursor(68, 20);
        display.print(getTimer());

        display.setTextSize(1);
        display.setCursor(38, 2);
        display.print("Get ready");
      }
      if (timerCount > 24) {
        display.setTextSize(5);
        display.setCursor(68, 20);
        display.print(getTimer());

        display.setTextSize(1);
        display.setCursor(35, 2);
        display.print("You missed");
      }

      if (tt >= 1 && timerCount <= 23) {
        if (tt == 8) {
          display.drawBitmap(17, 14, coffeeCup30_01, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 7) {
          display.drawBitmap(17, 14, coffeeCup30_02, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 6) {
          display.drawBitmap(17, 14, coffeeCup30_03, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 5) {
          display.drawBitmap(17, 14, coffeeCup30_04, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 4) {
          display.drawBitmap(17, 14, coffeeCup30_05, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 3) {
          display.drawBitmap(17, 14, coffeeCup30_06, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 2) {
          display.drawBitmap(17, 14, coffeeCup30_07, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 1) {
          display.drawBitmap(17, 14, coffeeCup30_08, 30, 30, WHITE);
          Serial.println(tt);
        }
        if (tt == 1 && timerCount <= 24) {
          tt = 8;
        } else {
          tt--;
        }
      } else {
        if (tt == 8) {
          display.drawBitmap(17, 14, coffeeCup30_09, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 7) {
          display.drawBitmap(17, 14, coffeeCup30_10, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 6) {
          display.drawBitmap(17, 14, coffeeCup30_11, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 5) {
          display.drawBitmap(17, 14, coffeeCup30_12, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 4) {
          display.drawBitmap(17, 14, coffeeCup30_13, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 3) {
          display.drawBitmap(17, 14, coffeeCup30_14, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 2) {
          display.drawBitmap(17, 14, coffeeCup30_15, 30, 30, WHITE);
          Serial.println(tt);
        } else if (tt == 1) {
          display.drawBitmap(17, 14, coffeeCup30_16, 30, 30, WHITE);
          Serial.println(tt);
        }
        if (tt == 1) {
          tt = 8;
        } else {
          tt--;
        }
      }

      if (maraData[3].toInt() < 100) {
        display.setCursor(19, 50);
      } else {
        display.setCursor(9, 50);
      }
      display.setTextSize(2);
      display.print(maraData[3].toInt());
      display.setTextSize(1);
      display.print((char)247);
      display.setTextSize(1);
      display.print("C");
    } else {
      //Coffee temperature and bitmap
      display.drawBitmap(17, 14, coffeeCup30_00, 30, 30, WHITE);
      if (maraData[3].toInt() < 100) {
        display.setCursor(19, 50);
      } else {
        display.setCursor(9, 50);
      }
      display.setTextSize(2);
      display.print(maraData[3].toInt());
      display.setTextSize(1);
      display.print((char)247);
      display.setTextSize(1);
      display.print("C");

      //Steam temperature and bitmap
      display.drawBitmap(83, 14, steam30, 30, 30, WHITE);
      if (maraData[1].toInt() < 100) {
        display.setCursor(88, 50);
      } else {
        display.setCursor(78, 50);
      }
      display.setTextSize(2);
      display.print(maraData[1].toInt());
      display.setTextSize(1);
      display.print((char)247);
      display.setTextSize(1);
      display.print("C");

      //Draw Line
      display.drawLine(66, 14, 66, 64, WHITE);

      //Boiler
      if (maraData[5].toInt() == 1) {
        display.setCursor(13, 0);
        display.setTextSize(1);
        display.print("Heating up");

        if ((millis() - lastToggleTime) > 1000) {
          lastToggleTime = millis();
          if (HeatDisplayToggle == 1) {
            HeatDisplayToggle = 0;
          } else {
            HeatDisplayToggle = 1;
          }
        }
        if (HeatDisplayToggle == 1) {
          display.fillRect(0, 0, 12, 12, BLACK);
          display.drawCircle(3, 3, 3, WHITE);
          display.fillCircle(3, 3, 2, WHITE);

        } else {
          display.fillRect(0, 0, 12, 12, BLACK);
          display.drawCircle(3, 3, 3, WHITE);
          // display.print("");
        }
      } else {
        display.print("");
        display.fillCircle(3, 3, 3, BLACK);

        //Draw machine mode
        if (maraData[0].substring(0, 1) == "C") {
          // Coffee mode
          display.drawBitmap(115, 0, coffeeCup12, 12, 12, WHITE);
        } else {
          // Steam mode
          display.drawBitmap(115, 0, steam12, 12, 12, WHITE);
        }
      }
    }
  }

  display.display();
  //publishWebpage();
}

void publishWebpage() {
  if (DEBUG == true) {
    Serial.println("Publish Webpage");
  }
  // You may note the Data of your own Module here
  // MAC C8-C9-A3-36-C0-58
  // ESP-36C058
  //
  WiFiClient client = server.available();
  if (!client.available()) {
    //Serial.println("WebServer - No Client");
    return; // There is no Client - Nothing to do
  }
  //Serial.println("Waiting for new client");
  //while (!client.available()) {
  //  delay(1);                 // This does block Webserver!
  //}
  Serial.println("Got new client");
  String request = client.readStringUntil('\r');
  Serial.println(request);
  client.flush();


  if (request.indexOf("/LEDON") != -1) {
    digitalWrite(LED_BUILTIN, on);  // Turn ON LED
    state = on;
  }
  if (request.indexOf("/LEDOFF") != -1) {
    digitalWrite(LED_BUILTIN, off);  // Turn OFF LED
    state = off;
  }

  /*------------------HTML Page Creation---------------------*/

  client.println("HTTP/1.1 200 OK");  // standalone web server with an ESP8266
  client.println("Content-Type: text/html");
  client.println("");
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
  //favicon.ico
  client.println("<link rel=\"shortcut icon\" href=\"data:image/x-icon;base64,AAABAAEAICAAAAAAAACoCAAAFgAAACgAAAAgAAAAQAAAAAEACAAAAAAAgAQAAAAAAAAAAAAAAAEAAAAAAAAAIUoAAClKAAAhUgAAJVYACC1KAAQtUgAAKVoACDVSAAgpWgAAMVoACDFaAAg5WgAAKWMACCljAAAxYwAIMWMAEC1OABAxUgAQOVIAEClaABgxSgAYMVIAGDlSACM2TwAQMVoAGDFaABA5WgAYOVoAITlaABApYwAQMWMAGDFjAAA5YwAIOWMAEDljABg5YwAhOWMAACFrAAApawAIKWsAEClrAAAxawAIMWsAEDFrAAA5awAIOWsAEDlrABg5awAAKXMAADFzAAAxewAMKXMACDFzAAU2dQAQMXMADDl3ACE5awAYNXMAITlzABAxewAQOXsAGDl7ABA5hAAYOYQAEEJaABhCUgAhQlIAHkRaAAhCYwAQQmMAEEJrAB5EYwAMRm8AFEJvACFCawAbTGsAHEJzABZGewAWTnUAHldzACk5UgApQlYAK0RnAC5MaAAxSnMAKVJzAClKewApUn8AKVprAC5UdQAhWoQAKWJ/ADFaewAxYoEAPGJ+AExmeQA5WowAQlqMADFjjAA7aYoALWKWADxllgBCa4wAPXGSAFJrhABKc4QAUnOEAEpvjABGb5QAVHOOAER1mQBCb60ASnuUAEeBnABSe5AAWH+YAGd7jABlfpYAXYGfAGuInABUgacAWoylAGKGpwBfg7UAYJGlAGiOpwBllK0AZpe4AHOMpQBzlKUAc4StAHOMrQBzlK0Ae5StAHucrQBzpa0Ae6WtAHOctQB7nLUAc5S9AHecuQBzlMYAc5y9AG+lvQB7nL0Ae6W9AHuUxgCElK0AhJytAISctQCEnL0AhKW9AIStvQCMnK0AjKW1AIyttQCMpb0AlKW9AJStvQCcrb0Ae5zGAHuczgB7pcYAda3LAISlxgCEoM4AjqrGAIylzgCErc4AiLHOAIy1zgCUrc4AlKXWAISt1gCJstYAjK3nAJS1xgCctcYAlLXOAJy1zgCMvdYAlrrTAJy11gCcvdYAnMbWAIy13gCMvd4AlL3eAJy93gCMxt4AnMbeAJTG5wCltb0ApbXGAKW1zgCttc4Apb3OAK29zgClxs4ArcbOAK211gClvdYArb3WALXO1gCltd4Apb3eAKXG3gCtxt4AtcbeAJzO3gCcvecApb3nAJzG5wClxucArcbnALXG5wCtzucAtc7nAK3W5wC11ucApcbvAKXO7wCtzu8ApM77ALXO7wC9ztYAvdbvAMbO5wDG5/cAzuf/AM73/wDW1ucA1uf/ANbv/wDW9/8A1v//AN7v/wDe9/8A3v//AOfv/wDn9/8A5///AO/37wDv9/cA7/f/AO///wD39+8A9/f3APf/9wD39/8A9///AP//7wD/9/cA///3AP/3/wD///8A//////j9/f/9//f18O3t7fHw8Pn+/v//////////////////+Pj//f/3+u/Pt7e3xM/w9Pn+//////////////////////3/+vTtXEUbQ0dBXnJ6d+r6///////////////////+//r68ZdECRwWQhRDQ3OAnvr///////////////////n/+vBUH471+vn69Pn59FCAqfT3/f/6+v/////////5//r18R8rtP3///r6//7+9XKjmvX3+vD6//////////33/vCcOja6+v/9/fr4/fr1Q0d2bavz+vj//////////fz/6VM9NKj//f/6+P348/VzjAUiZor1+Pz////////7//l6Lyc7qvj//////f36+u1w6R4vT4jG/v////////36+UoNOz+w/v7//v///fn58Gbq7UoLWYb+////////+vXsBjc7OcT//v7///3y+Pr0B5Xt6UUhXPT////////5//QFSCc65rGGhoWTlJrGy+5SG1kbRyMQ7f/////////9+rt/b2FNDCEpKyorHUpVXI6NHTkLc+r+/////////P/zrmQ3DQxJISotKysuGA8uWXuv6ebr9f7////////97Lc3KTQ7i7zw8fHw8PXw6nohTF11mvX6//////////7tGg43OoL19fr//v/+//X076geRXV09fr/////////+c0ETC1WeMnY6e709PDu2riXbXfZdF/4+vr////////58FMDIi9WZ258h5SVinxxY2YWyez17/r///////////zz+u13JCNGISIgIiMiRhoVc/T69fr6+v/////////////6+vTowxccJCIYShYbw/n5+vr6+v/////////////////////6+vnwT3nmker+/P34///////////////////++v//+v/6//CPj77Rtf7+/f///////////////////f3//////fz98Fjx9UJFlvT6////+v7//////////////f////////qxWPD1dyFszff//v//////////////////////////+iJu7eHxS2KK//r////////////////////////////1IW53aPAiCnX5+v////////////////////////////UtZEpo7akRdP//////////////////////////////9QNJBHrquUqA///////////////////////////////68O3toFcqXsn////////////////////////////////68OoKTC2k8PT///////////////////////////////39URkYI/D1+f///////////////////////////////f1QUs3t9PT+/////////////wAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\" />");
  client.println("<head>");
  client.println("<title>MaraX V2</title>");
  client.println("<meta http-equiv=\"refresh\" content=\"(2)\" >");  // Auto Page Refresh every 2 seconds
  client.println("</head>");
  // Begin Body
  client.print("<body>");
  client.println("<header id=\"main-header\">");
  client.println("<h1>MaraX V2</h1>");
  client.println("<h4>by www.m1n1.de</h4>");
  client.println("</header>");
 // Begin MaraX Data Display
   client.println("<div style=\"border: 3px solid black; padding: 10px;\"");
  if (isMaraOff == 1) {
    client.println("<p>MaraX is offline...</p>");
  } else {
    if (maraData[0].substring(0, 1) == "C") {
      client.println("<p>MaraX is in Coffee Mode</p>");
    } else {
      client.println("<p>MaraX is in Steam Mode</p>");
    }

    client.print("<p>MaraX Water Temp Value =  ");    // MaraX Hx Temperature
    client.print(maraData[3].toInt());
    client.println(" &deg;C</p>");
    client.print("<p>MaraX Steam Temp Value =  ");    // MaraX Steam Temperature
    client.print(maraData[1]);
    client.println(" &deg;C</p>");
  }
  client.println("</div>");
  client.println("<br>");
 // End MaraX Data Display
  if (state == on) {
    client.print("Led ON");
  } else {
    client.print("Led OFF");
  }
  client.print("<br>");
  client.print("<br>");
  client.println("<a href=\"/LEDON\"\"><button class=\"button\">ON</button></a>");
  client.println("<a href=\"/LEDOFF\"\"><button class=\"button\">OFF</button></a>");
 //
 //
  client.println("</body>");
  client.println("</html>");

  delay(1);
  Serial.println("Client disconnected");
  Serial.println("");
}


// Main Loop
void loop() {
  t.update();
  detectChanges();
  getMaraData();

}
