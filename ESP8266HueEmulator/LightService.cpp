#include "LightService.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <WiFiUdp.h>
#include "SSDP.h"
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT
#include <assert.h>
#include <FS.h>

#if PRINT_BUFFER_LEN < 4096
#  error aJson print buffer length PRINT_BUFFER_LEN must be increased to at least 4096
#endif

String macString;
String bridgeIDString;
String ipString;
String netmaskString;
String gatewayString;
// The username of the client (currently we authorize all clients simulating a pressed button on the bridge)
String client;


// Group and Scene file name prefixes for SPIFFS support 
String GROUP_FILE_TEMPLATE = "GROUP-%d.json";
String SCENE_FILE_TEMPLATE = "SCENE-%d.json";


ESP8266WebServer *HTTP;

struct rgbcolor {
  rgbcolor(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {};
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)
struct hsvcolor {
  hsvcolor(const rgbcolor& color) {
    float r = ((float)color.r)/COLOR_SATURATION;
    float g = ((float)color.g)/COLOR_SATURATION;
    float b = ((float)color.b)/COLOR_SATURATION;
    float mi = MIN(MIN(r,g),b);
    float ma = MAX(MAX(r,g),b);
    float diff = ma - mi;
    v = ma;
    h = 0;
    s = (!v)?0:(diff/ma);
    if (diff) {
      if (r == v) {
            h = (g - b) / diff + (g < b ? 6.0f : 0.0f);
        } else if (g == v) {
            h = (b - r) / diff + 2.0f;
        } else {
            h = (r - g) / diff + 4.0f;
        }
        h /= 6.0f;
    }
  };
  float h;
  float s;
  float v;
};

String removeSlashes(String uri) {
  if (uri[0] == '/') {
    uri = uri.substring(1);
  }
  if (uri.length() && uri[uri.length() - 1] == '/') {
    uri = uri.substring(0, uri.length() - 1);
  }
  return uri;
}

String getPathSegment(String uri) {
  // assume slashes removed
  int slash = uri.indexOf("/");
  if (slash == -1) {
    return uri;
  }
  return uri.substring(0, slash);
}

String removePathSegment(String uri) {
  // assume slashes removed
  int slash = uri.indexOf("/");
  if (slash == -1) {
    return "";
  }
  return uri.substring(slash);
}

String getWildCard(String requestUri, String wcUri, int n, char wildcard = '*') {
  wcUri = removeSlashes(wcUri);
  requestUri = removeSlashes(requestUri);
  String wildcardStr;
  wildcardStr += wildcard;
  int i = 0;
  while (1) {
    String uPath = getPathSegment(wcUri);
    String ruPath = getPathSegment(requestUri);
    if (uPath == wildcardStr) {
      if (i == n) {
        return ruPath;
      }
      i++;
    }
    wcUri = removeSlashes(removePathSegment(wcUri));
    requestUri = removeSlashes(removePathSegment(requestUri));
    if (!wcUri.length() && !requestUri.length()) {
      return "";
    }
    if (!wcUri.length() || !requestUri.length()) {
      return "";
    }
  }
  return "";
}

class WcFnRequestHandler;

typedef std::function<void(WcFnRequestHandler *handler, String requestUri, HTTPMethod method)> HandlerFunction;

class WcFnRequestHandler : public RequestHandler {
public:
    WcFnRequestHandler(HandlerFunction fn, const String &uri, HTTPMethod method, char wildcard = '*')
    : _fn(fn)
    , _uri(uri)
    , _method(method)
    , _wildcard(wildcard)
    {
      assert(_wildcard != '/');
      // verify that the URI is reasonable (only contains wildcard at the beginning/end/whole path segments
      for(size_t i = 0; i < _uri.length(); i++) {
        if (_uri[i] == _wildcard) {
          if (i != 0 && i != _uri.length() - 1 && (_uri[i-1] != '/' || _uri[i+1] != '/')) {
            assert(false);
          }
        }
      }
    }

    bool canHandle(HTTPMethod requestMethod, String requestUri) override  {
        if (_method != HTTP_ANY && _method != requestMethod) {
          return false;
        }

        String uri = removeSlashes(_uri);
        requestUri = removeSlashes(requestUri);
        String wildcardStr;
        wildcardStr += _wildcard;
        while (1) {
          String uPath = getPathSegment(uri);
          String ruPath = getPathSegment(requestUri);
          if (uPath != ruPath && uPath != wildcardStr) {
            return false;
          }
          uri = removeSlashes(removePathSegment(uri));
          requestUri = removeSlashes(removePathSegment(requestUri));
          if (!uri.length() && !requestUri.length()) {
            return true;
          }
          if (!uri.length() || !requestUri.length()) {
            return false;
          }
        }

        return true;
    }

    bool canUpload(String requestUri) override  {
        return false;
    }

    bool handle(ESP8266WebServer& server, HTTPMethod requestMethod, String requestUri) override {
        currentReqUri = requestUri;
        _fn(this, requestUri, requestMethod);
        currentReqUri = "";
        return true;
    }

    void upload(ESP8266WebServer& server, String requestUri, HTTPUpload& upload) override {}

    String getWildCard(int wcIndex) {
      return ::getWildCard(currentReqUri, _uri, wcIndex);
    }
protected:
    String currentReqUri;
    HandlerFunction _fn;
    String _uri;
    HTTPMethod _method;
    char _wildcard;
};

LightHandler *lightHandlers[MAX_LIGHT_HANDLERS] = {}; // interfaces exposed to the outside world

LightServiceClass::LightServiceClass() { }

bool LightServiceClass::setLightHandler(int index, LightHandler *handler) {
  if (index >= currentNumLights || index < 0) return false;
  lightHandlers[index] = handler;
  return true;
}


bool LightServiceClass::setLightsAvailable(int lights) {
  if (lights <= MAX_LIGHT_HANDLERS) {
    currentNumLights = lights;
    return true;
  }
  return false;
}

int LightServiceClass::getLightsAvailable() {
  return currentNumLights;
}

String StringIPaddress(IPAddress myaddr)
{
  String LocalIP = "";
  for (int i = 0; i < 4; i++)
  {
    LocalIP += String(myaddr[i]);
    if (i < 3) LocalIP += ".";
  }
  return LocalIP;
}

LightHandler *LightServiceClass::getLightHandler(int numberOfTheLight) {
  if (numberOfTheLight >= currentNumLights || numberOfTheLight < 0) {
    return nullptr;
  }

  if (!lightHandlers[numberOfTheLight]) {
    return new LightHandler();
  }

  return lightHandlers[numberOfTheLight];
}

static const char* _ssdp_response_template =
  "HTTP/1.1 200 OK\r\n"
  "EXT:\r\n"
  "CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL
  "LOCATION: http://%s:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  "SERVER: FreeRTOS/6.0.5, UPnP/1.0, %s/%s\r\n" // _modelName, _modelNumber
  "hue-bridgeid: %s\r\n"
  "ST: %s\r\n"  // _deviceType
  "USN: uuid:%s::upnp:rootdevice\r\n" // _uuid::_deviceType
  "\r\n";

static const char* _ssdp_notify_template =
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:alive\r\n"
  "CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL
  "LOCATION: http://%s:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  "SERVER: FreeRTOS/6.0.5, UPnP/1.0, %s/%s\r\n" // _modelName, _modelNumber
  "hue-bridgeid: %s\r\n"
  "NT: %s\r\n"  // _deviceType
  "USN: uuid:%s::upnp:rootdevice\r\n" // _uuid
  "\r\n";
  
static const char* _ssdp_xml_template = "<?xml version=\"1.0\" ?>"
  "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
  "<specVersion><major>1</major><minor>0</minor></specVersion>"
  "<URLBase>http://{ip}:80/</URLBase>"
  "<device>"
    "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
    "<friendlyName>Philips hue ({ip})</friendlyName>"
    "<manufacturer>Royal Philips Electronics</manufacturer>"
    "<manufacturerURL>http://www.philips.com</manufacturerURL>"
    "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
    "<modelName>Philips hue bridge 2012</modelName>"
    "<modelNumber>929000226503</modelNumber>"
    "<modelURL>http://www.meethue.com</modelURL>"
    "<serialNumber>{mac}</serialNumber>"
    "<UDN>uuid:2f402f80-da50-11e1-9b23-{mac}</UDN>"
    "<presentationURL>index.html</presentationURL>"
    "<iconList>"
    "  <icon>"
    "    <mimetype>image/png</mimetype>"
    "    <height>48</height>"
    "    <width>48</width>"
    "    <depth>24</depth>"
    "    <url>hue_logo_0.png</url>"
    "  </icon>"
    "  <icon>"
    "    <mimetype>image/png</mimetype>"
    "    <height>120</height>"
    "    <width>120</width>"
    "    <depth>24</depth>"
    "    <url>hue_logo_3.png</url>"
    "  </icon>"
    "</iconList>"
  "</device>"
  "</root>";

int ssdpMsgFormatCallback(SSDPClass *ssdp, char *buffer, int buff_len,
                          bool isNotify, int interval, char *modelName,
                          char *modelNumber, char *uuid, char *deviceType,
                          uint32_t ip, uint16_t port, char *schemaURL) {
  if (isNotify) {
    return snprintf(buffer, buff_len,
      _ssdp_notify_template,
      interval,
      ipString.c_str(), port, schemaURL,
      modelName, modelNumber,
      bridgeIDString.c_str(),
      deviceType,
      uuid);
  } else {
    return snprintf(buffer, buff_len,
      _ssdp_response_template,
      interval,
      ipString.c_str(), port, schemaURL,
      modelName, modelNumber,
      bridgeIDString.c_str(),
      deviceType,
      uuid);
  }
}

class LightGroup {
  public:
    LightGroup(aJsonObject *root) {
      aJsonObject* jName = aJson.getObjectItem(root, "name");
      aJsonObject* jLights = aJson.getObjectItem(root, "lights");
      // jName and jLights guaranteed to exist
      name = jName->valuestring;
      for (int i = 0; i < aJson.getArraySize(jLights); i++) {
        aJsonObject* jLight = aJson.getArrayItem(jLights, i);
        // lights are 1-based and map to the 0-based bitfield
        int lightNum = atoi(jLight->valuestring);
        if (lightNum != 0) {
          lights |= (1 << (lightNum - 1));
        }
      }
    }
    aJsonObject *getJson() {
      aJsonObject *object = aJson.createObject();
      aJson.addStringToObject(object, "name", name.c_str());
      aJsonObject *lightsArray = aJson.createArray();
      aJsonObject *lightsState = aJson.createObject();
      aJson.addStringToObject(lightsState, "all_on", "true");
      aJson.addStringToObject(lightsState, "any_on", "true");
      aJson.addItemToObject(object, "state", lightsState);
      
      aJson.addItemToObject(object, "lights", lightsArray);
      for (int i = 0; i < 16; i++) {
        if (!((1 << i) & lights)) {
          continue;
        }
        // add light to list
        String lightNum = "";
        lightNum += (i + 1);
        aJson.addItemToArray(lightsArray, aJson.createItem(lightNum.c_str()));
      }
      aJson.addStringToObject(object, "type", "LightGroup");   
      return object;
    }


    aJsonObject *getSceneJson(bool withStates) {
      aJsonObject *object = aJson.createObject();
      aJson.addStringToObject(object, "name", name.c_str());
      aJsonObject *lightsArray = aJson.createArray();
      aJson.addItemToObject(object, "lights", lightsArray);
      for (int i = 0; i < 16; i++) {
        if (!((1 << i) & lights)) {
          continue;
        }
        // add light to list
        String lightNum = "";
        lightNum += (i + 1);
        aJson.addItemToArray(lightsArray, aJson.createItem(lightNum.c_str()));
      }
            
      aJson.addStringToObject(object, "owner", "api");
      aJson.addBooleanToObject(object, "recycle", false);
      aJson.addBooleanToObject(object, "locked", false);
      aJsonObject *appData = aJson.createObject();
      aJson.addNumberToObject(appData, "version", 1);
      aJson.addStringToObject(appData, "data", "");
      aJson.addItemToObject(object, "appdata", appData);      
      aJson.addStringToObject(object, "picture", "");
      aJson.addStringToObject(object, "lastupdated", "2017-11-04T10:17:15");
      aJson.addNumberToObject(object, "version", 2);
      
      if(withStates==true)
      {
        Serial.println("Adding lightstates");
        aJsonObject *lightStates = aJson.createArray();  
        for (int i = 0; i < 16; i++) {
          if (!((1 << i) & lights)) {
            continue;
          }        
          // add light to list
          String lightNum = "";
          lightNum += (i + 1);
          LightHandler *handler = LightService.getLightHandler(i);
          HueLightInfo currentInfo = handler->getInfo(i);

          aJsonObject *lightState = aJson.createObject();
          aJsonObject *lightStateData = aJson.createObject();
          aJson.addBooleanToObject(lightStateData, "on", currentInfo.on);            
          aJson.addNumberToObject(lightStateData, "bri", currentInfo.brightness);
         
          aJsonObject *xy = aJson.createArray(); 
          aJson.addItemToArray(xy, aJson.createItem(0.5806));
          aJson.addItemToArray(xy, aJson.createItem(0.3903));
          aJson.addItemToObject(lightStateData,"xy",xy);
          aJson.addItemToObject(lightState, lightNum.c_str(), lightStateData);
          aJson.addItemToArray(lightStates, lightState);
          
        }        
        aJson.addItemToObject(object,"lightstates",lightStates);
      }
      
      char *json = aJson.print(object);
      //Serial.println(json);
      free(json);
      return object;
    }
    unsigned int getLightMask() {
      return lights;
    }
    // only used for scenes
    String id;
    String getName()
    {
      return name;  
    }
  private:
    String name;
    // use unsigned int to hold members of this group. 2 bytes -> supports up to 16 lights
    unsigned int lights = 0;
    // no need to hold the group type, only LightGroup is supported for API 1.4
};

void on(HandlerFunction fn, const String &wcUri, HTTPMethod method, char wildcard = '*') {
  HTTP->addHandler(new WcFnRequestHandler(fn, wcUri, method, wildcard));
}
 
String listFiles(String _template)
{
  Serial.println(_template);
  String response = "";
    for (int i = 0; i < 16; i++) {
      String fileName = _template;
      fileName.replace("%d",String(i));
      Serial.println(fileName);
      
      //sprintf(fileName,_template,i);
      if(!SPIFFS.exists(fileName))
        continue;
        
      Serial.println("Adding file to list");
      response += "<li>" + fileName + "</li>";
    
      
    }
    return response;
}

void indexPageFn() {
	String response = "<html><body>"
	"<h2>Philips HUE ( {ip} )</h2>"
	"<p>Available lights:</p>"
	"<ul>{lights}</ul>"
  "<ul>File Cache</ul>"
  "<ul>Groups:</ul>"
  "<ul>{group-files}</ul>"
  "<ul>Scenes:</ul>"
  "<ul>{scene-files}</ul>"
  "<a href='/cache/clear'>Clear Cached Groups and Scenes</a>"
	"</body></html>";
	
	String lights = "";
	
	 for (int i = 0; i < LightService.getLightsAvailable(); i++) {
	  if (!lightHandlers[i]) {
		  continue;
	  }
	  	  
	  lights += "<li>" + lightHandlers[i]->getFriendlyName(i) + "</li>";
	}
  String sceneFiles = listFiles(SCENE_FILE_TEMPLATE);
  String groupFiles = listFiles(GROUP_FILE_TEMPLATE);
	response.replace("{ip}", ipString);
	response.replace("{lights}", lights);
	response.replace("{group-files}", groupFiles);	
  response.replace("{scene-files}", sceneFiles);  
	HTTP->send(200, "text/html", response);
}

void clearFiles(String _template)
{
    for (int i = 0; i < 16; i++) {
      String fileName = _template;
      fileName.replace("%d",String(i));
      if(SPIFFS.exists(fileName)){
        SPIFFS.remove(fileName);   
      }
    }
}

// remove the group and scene cache files
void cacheClearFn()
{
  clearFiles(GROUP_FILE_TEMPLATE);
  clearFiles(SCENE_FILE_TEMPLATE);
  indexPageFn();
}

void descriptionFn() {
  String response = _ssdp_xml_template;

  String escapedMac = macString;
  escapedMac.replace(":", "");
  escapedMac.toLowerCase();
  
  response.replace("{mac}", escapedMac);
  response.replace("{ip}", ipString);

  HTTP->send(200, "text/xml", response);
  Serial.println(response);
}

void unimpFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  String str = "{}";
  HTTP->send(200, "text/plain", str);
  Serial.println(str);
}

aJsonObject *wrapWithSuccess(aJsonObject *body) {
  aJsonObject *success = aJson.createObject();
  aJson.addItemToObject(success, "success", body);
  return success;
}

// targetBase is assumed to have a trailing slash (/)
aJsonObject *generateTargetPutResponse(aJsonObject *body, String targetBase) {
  aJsonObject *root = aJson.createArray();
  for (int i = 0; i < aJson.getArraySize(body); i++) {
    aJsonObject *success = aJson.createObject();
    aJson.addItemToArray(root, wrapWithSuccess(success));
    aJsonObject *entry = aJson.getArrayItem(body, i);
    String target = targetBase + entry->name;
    switch (entry->type) {
      case aJson_Boolean:
        aJson.addBooleanToObject(success, target.c_str(), entry->valuebool);
        break;
      case aJson_Int:
        aJson.addNumberToObject(success, target.c_str(), entry->valueint);
        break;
      case aJson_String:
        aJson.addStringToObject(success, target.c_str(), entry->valuestring);
        break;
      case aJson_Float:
        aJson.addNumberToObject(success, target.c_str(), entry->valuefloat);
        break;
      case aJson_Array: {
        aJsonObject *xy = aJson.createArray();
        aJson.addItemToObject(success, target.c_str(), xy);
        for (int j = 0; j < aJson.getArraySize(entry); j++) {
          aJson.addItemToArray(xy, aJson.createItem(aJson.getArrayItem(entry, j)->valuefloat));
        }
        break;
      }
      default:
        break;
    }
  }
  return root;
}

void addConfigJson(aJsonObject *config);
void sendJson(aJsonObject *config);
void sendUpdated();
void sendError(int type, String path, String description);
void configFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  switch (method) {
    case HTTP_GET: {
      aJsonObject *root;
      root = aJson.createObject();
      addConfigJson(root);
      sendJson(root);
      break;
    }
    case HTTP_PUT: {
      Serial.print("configFn:");
      Serial.println(HTTP->arg("plain"));
      aJsonObject* body = aJson.parse(( char*) HTTP->arg("plain").c_str());
      sendJson(generateTargetPutResponse(body, "/config/"));
      aJson.deleteItem(body);
      // TODO: actually store this
      break;
    }
    default:
      sendError(4, requestUri, "Config method not supported");
      break;
  }
}

void sendSuccess(String name, String value);
void authFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  // On the real bridge, the link button on the bridge must have been recently pressed for the command to execute successfully.
  // We try to execute successfully regardless of a button for now.
  sendSuccess("username", "api");
}

aJsonObject *getGroupJson();
aJsonObject *getSceneJson();
void addLightsJson(aJsonObject *config);
void wholeConfigFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  // Serial.println("Respond with complete json as in https://github.com/probonopd/ESP8266HueEmulator/wiki/Hue-API#get-all-information-about-the-bridge");
  aJsonObject *root;
  root = aJson.createObject();
  // the default group 0 is never listed
  aJsonObject *lights;
  aJson.addItemToObject(root, "lights", lights = aJson.createObject());
  addLightsJson(lights);
  aJson.addItemToObject(root, "groups", getGroupJson());
  aJsonObject *config;
  aJson.addItemToObject(root, "config", config = aJson.createObject());
  addConfigJson(config);
  aJsonObject *schedules;
  aJson.addItemToObject(root, "schedules", schedules = aJson.createObject());
  aJson.addItemToObject(root, "scenes", getSceneJson());
  aJsonObject *rules;
  aJson.addItemToObject(root, "rules", rules = aJson.createObject());
  aJsonObject *sensors;
  aJson.addItemToObject(root, "sensors", sensors = aJson.createObject());
  aJsonObject *resourceLinks;
  aJson.addItemToObject(root, "resourcelinks", resourceLinks = aJson.createObject());
  sendJson(root);
}

void sceneListingHandler();
void sceneCreationHandler(String body);
String scenePutHandler(String body);
void scenesFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  String id = "";
  switch (method) {
    case HTTP_GET:
      sceneListingHandler();
      break;
    case HTTP_PUT:
      id = scenePutHandler("");
      break;
    case HTTP_POST:
      sceneCreationHandler("");
      break;
    default:
      sendError(4, requestUri, "Scene method not supported");
      break;
  }
}

int findSceneIndex(String id);
LightGroup *findScene(String id);
bool updateSceneSlot(int slot, String id, String body);
void sendSuccess(String text);
void sceneCreationHandler(String sceneId);
void scenesIdFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  String sceneId = handler->getWildCard(1);
  LightGroup *scene = findScene(sceneId);
  switch (method) {
    case HTTP_GET:
      if (scene) {
        sendJson(scene->getSceneJson(true));
      } else {
        sendError(3, "/scenes/"+sceneId, "Cannot retrieve scene that does not exist");
      }
      break;
    case HTTP_PUT:
      // validate body, delete old group, create new group
      sceneCreationHandler(sceneId);
      // XXX not a valid response according to API
      sendUpdated();
      break;
    case HTTP_DELETE:
      if (scene) {
        updateSceneSlot(findSceneIndex(sceneId), sceneId, "");
      } else {
        sendError(3, requestUri, "Cannot delete scene that does not exist");
      }
      sendSuccess(requestUri+" deleted");
      break;
    default:
      sendError(4, requestUri, "Scene method not supported");
      break;
  }
}

void scenesIdLightFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  switch (method) {
    case HTTP_PUT: {
      Serial.print("Body: ");
      Serial.println(HTTP->arg("plain"));
      // XXX Do something with this information...
      aJsonObject* body = aJson.parse(( char*) HTTP->arg("plain").c_str());
      sendJson(generateTargetPutResponse(body, "/scenes/" + handler->getWildCard(1) + "/lightstates/" + handler->getWildCard(2) + "/"));
      aJson.deleteItem(body);
      break;
    }
    default:
      sendError(4, requestUri, "Scene method not supported");
      break;
  }
}

void groupListingHandler();
void groupCreationHandler();
void groupsFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      groupListingHandler();
      break;
    case HTTP_POST:
      groupCreationHandler();
      break;
    default:
      sendError(4, requestUri, "Group method not supported");
      break;
  }
}

LightGroup *lightGroups[16] = {nullptr, };
void groupCreationHandler(String sceneId);
bool updateGroupSlot(int slot, String body);
void groupsIdFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  String groupNumText = handler->getWildCard(1);
  int groupNum = atoi(groupNumText.c_str()) - 1;
  if ((groupNum == -1 && groupNumText != "0") || groupNum >= 16 || (groupNum >= 0 && !lightGroups[groupNum])) {
    // error, invalid group number
    sendError(3, requestUri, "Invalid group number");
    return;
  }

  switch (method) {
    case HTTP_GET:
      if (groupNum != -1) {
        sendJson(lightGroups[groupNum]->getJson());
      } else {
        aJsonObject *object = aJson.createObject();
        aJson.addStringToObject(object, "name", "0");
        aJsonObject *lightsArray = aJson.createArray();
        aJson.addItemToObject(object, "lights", lightsArray);
        for (int i = 0; i < LightService.getLightsAvailable(); i++) {
          if (!lightHandlers[i]) {
            continue;
          }
          // add light to list
          String lightNum = "";
          lightNum += (i + 1);
          aJson.addItemToArray(lightsArray, aJson.createItem(lightNum.c_str()));
        }
        sendJson(object);
      }
      break;
    case HTTP_PUT:
      // validate body, delete old group, create new group
      updateGroupSlot(groupNum, HTTP->arg("plain"));
      sendUpdated();
      break;
    case HTTP_DELETE:
      updateGroupSlot(groupNum, "");
      sendSuccess(requestUri+" deleted");
      break;
    default:
      sendError(4, requestUri, "Group method not supported");
      break;
  }
}

void applyConfigToLightMask(unsigned int lights);
void groupsIdActionFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  if (method != HTTP_PUT) {
    // error, only PUT allowed
    sendError(4, requestUri, "Only PUT supported for groups/*/action");
    return;
  }

  String groupNumText = handler->getWildCard(1);
  int groupNum = atoi(groupNumText.c_str()) - 1;
  if ((groupNum == -1 && groupNumText != "0") || groupNum >= 16 || (groupNum >= 0 && !lightGroups[groupNum])) {
    // error, invalid group number
    sendError(3, requestUri, "Invalid group number");
    return;
  }
  // parse input as if for all lights
  unsigned int lightMask;
  if (groupNum == -1) {
    lightMask == 0xFFFF;
  } else {
    
    lightMask = lightGroups[groupNum]->getLightMask();
  }
  // apply to group
  applyConfigToLightMask(lightMask);
}

void lightsFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  switch (method) {
    case HTTP_GET: {
      // dump existing lights
      aJsonObject *lights = aJson.createObject();
      addLightsJson(lights);
      sendJson(lights);
      break;
    }
    case HTTP_POST:
      // "start" a "search" for "new" lights
      sendSuccess("/lights", "Searching for new devices");
      break;
    default:
      sendError(4, requestUri, "Light method not supported");
      break;
  }
}

void addSingleLightJson(aJsonObject* , int , LightHandler *);
void lightsIdFn(WcFnRequestHandler *whandler, String requestUri, HTTPMethod method) {
  int numberOfTheLight = atoi(whandler->getWildCard(1).c_str()) - 1;
  LightHandler *handler = LightService.getLightHandler(numberOfTheLight);
  switch (method) {
    case HTTP_GET: {
      aJsonObject *root;
      root = aJson.createObject();
      addSingleLightJson(root, numberOfTheLight, handler);
      sendJson(root);
      break;
    }
    case HTTP_PUT:
      // XXX do something here
      sendUpdated();
      break;
    default:
      sendError(4, requestUri, "Light method not supported");
      break;
  }
}

void lightsIdStateFn(WcFnRequestHandler *whandler, String requestUri, HTTPMethod method) {
  int numberOfTheLight = max(0, atoi(whandler->getWildCard(1).c_str()) - 1);
  LightHandler *handler = LightService.getLightHandler(numberOfTheLight);
  if (!handler) {
    char buff[100] = {};
    sprintf(buff, "Requested light not available for number %d", numberOfTheLight);
    sendError(3, requestUri, buff);
    return;
  }

  switch (method) {
    case HTTP_POST:
    case HTTP_PUT: { 
      aJsonObject* parsedRoot = aJson.parse( (char*) HTTP->arg("plain").c_str());   
      if (!parsedRoot) {
        // unparseable json
          sendError(2, requestUri, "Bad JSON body in request");
          return;       
      }
      HueLightInfo currentInfo = handler->getInfo(numberOfTheLight);
      HueLightInfo newInfo;
      if (!parseHueLightInfo(currentInfo, parsedRoot, &newInfo)) {
        aJson.deleteItem(parsedRoot);
        return;
      }
      
      handler->handleQuery(numberOfTheLight, newInfo, parsedRoot);
      sendJson(generateTargetPutResponse(parsedRoot, "/lights/" + whandler->getWildCard(1) + "/state/"));
      aJson.deleteItem(parsedRoot);
      break;
    }
    default:
      sendError(4, requestUri, "Light method not supported");
      break;
  }
}

void lightsNewFn(WcFnRequestHandler *handler, String requestUri, HTTPMethod method) {
  // dump empty object
  aJsonObject *lights = aJson.createObject();
  sendJson(lights);
}

void LightServiceClass::begin() {
  begin(new ESP8266WebServer(80));
}

void initializeGroupSlots();
void initializeSceneSlots();
void LightServiceClass::begin(ESP8266WebServer *svr) {
  HTTP = svr;
  macString = WiFi.macAddress();
  bridgeIDString = macString;
  bridgeIDString.replace(":", "");
  bridgeIDString = bridgeIDString.substring(0, 6) + "FFFE" + bridgeIDString.substring(6);
  ipString = StringIPaddress(WiFi.localIP());
  netmaskString = StringIPaddress(WiFi.subnetMask());
  gatewayString = StringIPaddress(WiFi.gatewayIP());

  Serial.print("Starting HTTP at ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(80);

  HTTP->on("/index.html", HTTP_GET, indexPageFn);
  HTTP->on("/cache/clear", HTTP_GET, cacheClearFn);
  HTTP->on("/description.xml", HTTP_GET, descriptionFn);
  on(configFn, "/api/*/config", HTTP_ANY);
  on(configFn, "/api/config", HTTP_GET);
  on(wholeConfigFn, "/api/*", HTTP_GET);
  on(wholeConfigFn, "/api/", HTTP_GET);
  on(authFn, "/api", HTTP_POST);
  on(unimpFn, "/api/*/schedules", HTTP_GET);
  on(unimpFn, "/api/*/rules", HTTP_GET);
  on(unimpFn, "/api/*/sensors", HTTP_GET);
  on(scenesFn, "/api/*/scenes", HTTP_ANY);
  on(scenesIdFn, "/api/*/scenes/*", HTTP_ANY);
  on(scenesIdLightFn, "/api/*/scenes/*/lightstates/*", HTTP_ANY);
  on(scenesIdLightFn, "/api/*/scenes/*/lights/*/state", HTTP_ANY);
  on(groupsFn, "/api/*/groups", HTTP_ANY);
  on(groupsIdFn, "/api/*/groups/*", HTTP_ANY);
  on(groupsIdActionFn, "/api/*/groups/*/action", HTTP_ANY);
  on(lightsFn, "/api/*/lights", HTTP_ANY);
  on(lightsNewFn, "/api/*/lights/new", HTTP_ANY);
  on(lightsIdFn, "/api/*/lights/*", HTTP_ANY);
  on(lightsIdStateFn, "/api/*/lights/*/state", HTTP_ANY);

  HTTP->begin();

  String serial = macString;
  serial.toLowerCase();
  
  Serial.println("Starting SSDP...");
  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName("Philips hue clone");
  SSDP.setSerialNumber(serial.c_str());
  SSDP.setURL("index.html");
  SSDP.setModelName("IpBridge");
  SSDP.setModelNumber("0.1");
  SSDP.setModelURL("http://www.meethue.com");
  SSDP.setManufacturer("Royal Philips Electronics");
  SSDP.setManufacturerURL("http://www.philips.com");

  //SSDP.setDeviceType((char*)"upnp:rootdevice");
  SSDP.setDeviceType("urn:schemas-upnp-org:device:basic:1");
  //SSDP.setMessageFormatCallback(ssdpMsgFormatCallback);
  SSDP.begin();
  Serial.println("SSDP Started");
  Serial.println("FS Starting");
  SPIFFS.begin();
  initializeGroupSlots();
  initializeSceneSlots();

}

void LightServiceClass::update() {
  HTTP->handleClient();
}

void sendJson(aJsonObject *root) {
  // Take aJsonObject and print it to Serial and to WiFi
  // From https://github.com/pubnub/msp430f5529/blob/master/msp430f5529.ino
  char *msgStr = aJson.print(root);
  aJson.deleteItem(root);
  Serial.println(millis());
  Serial.println(msgStr);
  HTTP->send(200, "application/json", msgStr);
  free(msgStr);
}

// ==============================================================================================================
// Color Conversion
// ==============================================================================================================
// TODO: Consider switching to something along the lines of
// https://github.com/patdie421/mea-edomus/blob/master/src/philipshue_color.c
// and/ or https://github.com/kayno/arduinolifx/blob/master/color.h
// for color coversions instead
// ==============================================================================================================

// Based on http://stackoverflow.com/questions/22564187/rgb-to-philips-hue-hsb
// The code is based on this brilliant note: https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/commit/f41091cf671e13fe8c32fcced12604cd31cceaf3

rgbcolor getXYtoRGB(float x, float y, int brightness_raw) {
  float Y = brightness_raw / 250.0f;

  float z = 1.0f - x - y;
  float X = (Y / y) * x;
  float Z = (Y / y) * z;

  // sRGB D65 conversion
  // See https://en.wikipedia.org/wiki/SRGB
  float r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
  float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
  float b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;

  if (r > b && r > g && r > 1.0f) {
    // red is too big
    g = g / r;
    b = b / r;
    r = 1.0f;
  }
  else if (g > b && g > r && g > 1.0f) {
    // green is too big
    r = r / g;
    b = b / g;
    g = 1.0f;
  }
  else if (b > r && b > g && b > 1.0f) {
    // blue is too big
    r = r / b;
    g = g / b;
    b = 1.0f;
  }

  // Apply gamma correction
  r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
  g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
  b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;

  if (r > b && r > g) {
    // red is biggest
    if (r > 1.0f) {
      g = g / r;
      b = b / r;
      r = 1.0f;
    }
  }
  else if (g > b && g > r) {
    // green is biggest
    if (g > 1.0f) {
      r = r / g;
      b = b / g;
      g = 1.0f;
    }
  }
  else if (b > r && b > g) {
    // blue is biggest
    if (b > 1.0f) {
      r = r / b;
      g = g / b;
      b = 1.0f;
    }
  }

  r = r < 0 ? 0 : r;
  g = g < 0 ? 0 : g;
  b = b < 0 ? 0 : b;

  return rgbcolor(r * COLOR_SATURATION,
                  g * COLOR_SATURATION,
                  b * COLOR_SATURATION);
}

int getHue(hsvcolor hsb) {
  return hsb.h * 360 * 182.04;
}

int getSaturation(hsvcolor hsb) {
  return hsb.s * COLOR_SATURATION;
}

rgbcolor getMirektoRGB(int mirek) {
  int hectemp = 10000 / mirek;
  int r, g, b;
  if (hectemp <= 66) {
    r = COLOR_SATURATION;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = COLOR_SATURATION;
  }
  r = r > COLOR_SATURATION ? COLOR_SATURATION : r;
  g = g > COLOR_SATURATION ? COLOR_SATURATION : g;
  b = b > COLOR_SATURATION ? COLOR_SATURATION : b;
  return rgbcolor(r, g, b);
}

void sendError(int type, String path, String description) {
  aJsonObject *root = aJson.createArray();
  aJsonObject *errorContainer = aJson.createObject();
  aJsonObject *errorObject = aJson.createObject();
  aJson.addItemToObject(errorObject, "type", aJson.createItem(type));
  aJson.addStringToObject(errorObject, "address", path.c_str());
  aJson.addStringToObject(errorObject, "description", description.c_str());
  aJson.addItemToObject(errorContainer, "error", errorObject);
  aJson.addItemToArray(root, errorContainer);
  sendJson(root);
}

void sendSuccess(String id, String value) {
  aJsonObject *search = aJson.createArray();
  aJsonObject *container = aJson.createObject();
  aJson.addItemToArray(search, container);
  aJsonObject *succeed = aJson.createObject();
  aJson.addItemToObject(container, "success", succeed);
  aJson.addStringToObject(succeed, id.c_str(), value.c_str());
  sendJson(search);
}

void sendSuccess(String value) {
  aJsonObject *search = aJson.createArray();
  aJsonObject *container = aJson.createObject();
  aJson.addItemToArray(search, container);
  aJsonObject *succeed = aJson.createObject();
  aJson.addStringToObject(container, "success", value.c_str());
  sendJson(search);
}

void sendUpdated() {
  Serial.println("Updated.");
  HTTP->send(200, "text/plain", "Updated.");
}

bool parseHueLightInfo(HueLightInfo currentInfo, aJsonObject *parsedRoot, HueLightInfo *newInfo) {
  *newInfo = currentInfo;
  aJsonObject* onState = aJson.getObjectItem(parsedRoot, "on");
  if (onState) {
    newInfo->on = onState->valuebool;
  }

  // pull brightness
  aJsonObject* briState = aJson.getObjectItem(parsedRoot, "bri");
  if (briState) {
    newInfo->brightness = briState->valueint;
  }

  // pull effect
  aJsonObject* effectState = aJson.getObjectItem(parsedRoot, "effect");
  if (effectState) {
    const char *effect = effectState->valuestring;
    if (!strcmp(effect, "colorloop")) {
      newInfo->effect = EFFECT_COLORLOOP;
    } else {
      newInfo->effect = EFFECT_NONE;
    }
  }

  // pull alert
  aJsonObject* alertState = aJson.getObjectItem(parsedRoot, "alert");
  if (alertState) {
    const char *alert = alertState->valuestring;
    if (!strcmp(alert, "select")) {
      newInfo->alert = ALERT_SELECT;
    } else if (!strcmp(alert, "lselect")) {
      newInfo->alert = ALERT_LSELECT;
    } else {
      newInfo->alert = ALERT_NONE;
    }
  }

  // pull transitiontime
  aJsonObject* transitiontimeState = aJson.getObjectItem(parsedRoot, "transitiontime");
  if (transitiontimeState) {
    newInfo->transitionTime = transitiontimeState->valueint;
  }

  aJsonObject* hueState = aJson.getObjectItem(parsedRoot, "hue");
  aJsonObject* satState = aJson.getObjectItem(parsedRoot, "sat");
  aJsonObject* ctState = aJson.getObjectItem(parsedRoot, "ct");
  aJsonObject* xyState = aJson.getObjectItem(parsedRoot, "xy");
  if (xyState) {
    aJsonObject* elem0 = aJson.getArrayItem(xyState, 0);
    aJsonObject* elem1 = aJson.getArrayItem(xyState, 1);
    if (!elem0 || !elem1) {
      sendError(5, "/api/api/lights/?/state", "xy color coordinates incomplete");
      return false;
    }
    hsvcolor hsb = getXYtoRGB(elem0->valuefloat, elem1->valuefloat, newInfo->brightness);
    newInfo->hue = getHue(hsb);
    newInfo->saturation = getSaturation(hsb);
  } else if (ctState) {
    int mirek = ctState->valueint;
    if (mirek > 500 || mirek < 153) {
      sendError(7, "/api/api/lights/?/state", "Invalid value for color temperature");
      return false;
    }

    hsvcolor hsb = getMirektoRGB(mirek);
    newInfo->hue = getHue(hsb);
    newInfo->saturation = getSaturation(hsb);
  } else if (hueState || satState) {
    if (hueState) newInfo->hue = hueState->valueint;
    if (satState) newInfo->saturation = satState->valueint;
  }
  return true;
}



void addSingleLightJson(aJsonObject* root, int numberOfTheLight, LightHandler *lightHandler) {
  if (!lightHandler) return;
  String lightNumber = (String) (numberOfTheLight + 1);
  String lightName = lightHandler->getFriendlyName(numberOfTheLight);
  
  aJson.addStringToObject(root, "manufacturername", "OpenSource"); // type of lamp (all "Extended colour light" for now)
  aJson.addStringToObject(root, "modelid", "LST001"); // the model number
  aJson.addStringToObject(root, "name",  lightName.c_str()); // // the name as set through the web UI or app
  aJsonObject *state;
  aJson.addItemToObject(root, "state", state = aJson.createObject());
  HueLightInfo info = lightHandler->getInfo(numberOfTheLight);
  aJson.addBooleanToObject(state, "on", info.on);
  aJson.addNumberToObject(state, "hue", info.hue); // hs mode: the hue (expressed in ~deg*182.04)
  aJson.addNumberToObject(state, "bri", info.brightness); // brightness between 0-254 (NB 0 is not off!)
  aJson.addNumberToObject(state, "sat", info.saturation); // hs mode: saturation between 0-254
  double numbers[2] = {0.0, 0.0};
  aJson.addItemToObject(state, "xy", aJson.createFloatArray(numbers, 2)); // xy mode: CIE 1931 color co-ordinates
  aJson.addNumberToObject(state, "ct", 500); // ct mode: color temp (expressed in mireds range 154-500)
  aJson.addStringToObject(state, "alert", "none"); // 'select' flash the lamp once, 'lselect' repeat flash for 30s
  aJson.addStringToObject(state, "effect", info.effect == EFFECT_COLORLOOP ? "colorloop" : "none");
  aJson.addStringToObject(state, "colormode", "hs"); // the current color mode
  aJson.addBooleanToObject(state, "reachable", true); // lamp can be seen by the hub  aJson.addStringToObject(root, "type", "Extended color light"); // type of lamp (all "Extended colour light" for now)
  
  aJson.addStringToObject(root, "swversion", "0.1"); // type of lamp (all "Extended colour light" for now)
  if (info.bulbType == HueBulbType::DIMMABLE_LIGHT) {
    aJson.addStringToObject(root, "type", "Dimmable light");
  } else {
    aJson.addStringToObject(root, "type", "Extended color light");
  }
  aJson.addStringToObject(root, "uniqueid",  lightNumber.c_str());

}


void addLightJson(aJsonObject* root, int numberOfTheLight, LightHandler *lightHandler) {
  if (!lightHandler) 
    return;

  
  String lightNumber = (String) (numberOfTheLight + 1);
  String lightName = lightHandler->getFriendlyName(numberOfTheLight);
  aJsonObject *light;
  aJson.addItemToObject(root, lightNumber.c_str(), light = aJson.createObject());

  HueLightInfo info = lightHandler->getInfo(numberOfTheLight);
  if (info.bulbType == HueBulbType::DIMMABLE_LIGHT) {
    aJson.addStringToObject(light, "type", "Dimmable light");
  } else {
    aJson.addStringToObject(light, "type", "Extended color light");
  }
  
  aJson.addStringToObject(light, "manufacturername", "OpenSource"); // type of lamp (all "Extended colour light" for now)
  aJson.addStringToObject(light, "swversion", "0.1");
  aJson.addStringToObject(light, "name",  lightName.c_str()); // // the name as set through the web UI or app
  aJson.addStringToObject(light, "uniqueid",  (macString + "-" + (String) (numberOfTheLight + 1)).c_str());
  aJson.addStringToObject(light, "modelid", "LST001"); // the model number
  
  aJsonObject *state;
  aJson.addItemToObject(light, "state", state = aJson.createObject());
  aJson.addBooleanToObject(state, "on", info.on);

  aJson.addNumberToObject(state, "bri", info.brightness); // brightness between 0-254 (NB 0 is not off!)

  if (info.bulbType == HueBulbType::EXTENDED_COLOR_LIGHT) {
    double numbers[2] = {0.0, 0.0};
    aJson.addItemToObject(state, "xy", aJson.createFloatArray(numbers, 2)); // xy mode: CIE 1931 color co-ordinates
    aJson.addStringToObject(state, "colormode", "hs"); // the current color mode
    aJson.addStringToObject(state, "effect", info.effect == EFFECT_COLORLOOP ? "colorloop" : "none");
    aJson.addNumberToObject(state, "ct", 500); // ct mode: color temp (expressed in mireds range 154-500)

    aJson.addNumberToObject(state, "hue", info.hue); // hs mode: the hue (expressed in ~deg*182.04)
    aJson.addNumberToObject(state, "sat", info.saturation); // hs mode: saturation between 0-254
  }
  
  aJson.addStringToObject(state, "alert", "none"); // 'select' flash the lamp once, 'lselect' repeat flash for 30s
  aJson.addBooleanToObject(state, "reachable", true); // lamp can be seen by the hub
}

void addLightsJson(aJsonObject *lights) {
  for (int i = 0; i < LightService.getLightsAvailable(); i++) {
    addLightJson(lights, i, LightService.getLightHandler(i));
  }
}

static String format2Digits(int num) {
  String s = "";

  if (num < 10) {
    s += "0";
  }
  s += String(num);
  return s;
}

void addConfigJson(aJsonObject *root)
{
  aJson.addStringToObject(root, "name", "hue emulator");
  aJson.addStringToObject(root, "swversion", "81012917");
  aJson.addStringToObject(root, "bridgeid", bridgeIDString.c_str());
  aJson.addBooleanToObject(root, "portalservices", false);
  aJson.addBooleanToObject(root, "linkbutton", true);
  aJson.addStringToObject(root, "mac", macString.c_str());
  aJson.addBooleanToObject(root, "dhcp", true);
  aJson.addStringToObject(root, "ipaddress", ipString.c_str());
  aJson.addStringToObject(root, "netmask", netmaskString.c_str());
  aJson.addStringToObject(root, "gateway", gatewayString.c_str());
  aJson.addStringToObject(root, "apiversion", "1.3.0");
  if (timeStatus() == timeSet) {
    time_t moment = now();
    String dts = String(year(moment));
    dts += "-";
    dts += format2Digits(month(moment));
    dts += "-";
    dts += format2Digits(day(moment));
    dts += "T";
    dts += NTP.getTimeStr(moment);

    // TODO: send this as "utc" and "localtime" as timezone corrected utc
    aJson.addStringToObject(root, "localtime", dts.c_str());
  }
  // TODO: take this from the settings, once we have spiffs support
  aJson.addStringToObject(root, "timezone", "Europe/London");
  aJsonObject *whitelist;
  aJson.addItemToObject(root, "whitelist", whitelist = aJson.createObject());
  aJsonObject *whitelistFirstEntry;
  aJson.addItemToObject(whitelist, "api", whitelistFirstEntry = aJson.createObject());
  aJson.addStringToObject(whitelistFirstEntry, "name", "clientname#devicename");
  aJsonObject *swupdate;
  aJson.addItemToObject(root, "swupdate", swupdate = aJson.createObject());
  aJson.addStringToObject(swupdate, "text", "");
  aJson.addBooleanToObject(swupdate, "notify", false); // Otherwise client app shows update notice
  aJson.addNumberToObject(swupdate, "updatestate", 0);
  aJson.addStringToObject(swupdate, "url", "");
}

String trimSlash(String uri) {
  if (uri.startsWith("/")) {
    uri.remove(0, 1);
  }
  return uri;
}

aJsonObject *validateGroupCreateBody(String body) {
  aJsonObject *root = aJson.parse(( char*) body.c_str());
  aJsonObject* jName = aJson.getObjectItem(root, "name");
  aJsonObject* jLights = aJson.getObjectItem(root, "lights");
  if (!jName || !jLights) {
    return nullptr;
  }
  return root;
}

void applyConfigToLightMask(unsigned int lights) {
  Serial.print("applyConfigToLightMask:");
  Serial.println(HTTP->arg("plain"));
  aJsonObject* parsedRoot = aJson.parse(( char*) HTTP->arg("plain").c_str());
  if (parsedRoot) {
    for (int i = 0; i < LightService.getLightsAvailable(); i++) {
      LightHandler *handler = LightService.getLightHandler(i);
      HueLightInfo currentInfo = handler->getInfo(i);
      HueLightInfo newInfo;
      if (parseHueLightInfo(currentInfo, parsedRoot, &newInfo)) {
        handler->handleQuery(i, newInfo, parsedRoot);
      }
    }
    aJson.deleteItem(parsedRoot);

    // As per the spec, the response can be "Updated." for memory-constrained devices
    sendUpdated();
  } else if (HTTP->arg("plain") != "") {
    // unparseable json
    sendError(2, "groups/0/action", "Bad JSON body in request");
  }
}
// check the file system and restore group slots from them
void  initializeGroupSlots()
{
  Serial.println("initializeGroupSlots()");
  for (int i = 0; i < 16; i++) {
    String fileName = GROUP_FILE_TEMPLATE;
    fileName.replace("%d",String(i));
    //Serial.print("Testing for ");Serial.println(fileName);
    if (SPIFFS.exists(fileName)) {   
      // read the file into the groupslot position
      File f  = SPIFFS.open(fileName,"r");
      if(f)
      {
        size_t fsize = f.size();
        std::unique_ptr<char[]> buf (new char[fsize]);
        Serial.println("Reading in to buffer");
        f.readBytes(buf.get(), fsize);    
        lightGroups[i] = new LightGroup(aJson.parse(buf.get()));
        f.close();
        Serial.print("Loading "); Serial.println(fileName);
      }
      else
      {
        Serial.println("Can't open");
      }

    }
    else
    {
      //Serial.println("Not found");
    }  
  }
}


LightGroup *lightScenes[16] = {nullptr, };

// check the file system and restore group slots from them
void  initializeSceneSlots()
{
  Serial.println("initializeSceneSlots()");
  for (int i = 0; i < 16; i++) {
    String fileName = SCENE_FILE_TEMPLATE;
    fileName.replace("%d",String(i));
    //Serial.print("Testing for ");Serial.println(fileName);
    if (SPIFFS.exists(fileName)) {   
      // read the file into the groupslot position
      File f  = SPIFFS.open(fileName,"r");
      if(f)
      {
        size_t fsize = f.size();
        std::unique_ptr<char[]> buf (new char[fsize]);
        Serial.println("Reading in to buffer");
        f.readBytes(buf.get(), fsize);            
        lightScenes[i] = new LightGroup(aJson.parse(buf.get()));
        lightScenes[i]->id = String(i,DEC);
        f.close();
        Serial.print("Loading "); Serial.println(fileName);
        
      }
      else
      {
        Serial.println("Can't open");
      }

    }
    else
    {
      //Serial.println("Not found");
    }  
  }
}

// returns true on failure
bool updateGroupSlot(int slot, String body) {
  String fileName = GROUP_FILE_TEMPLATE;
  fileName.replace("%d",String(slot));
  
  aJsonObject *root;
  if (body != "") {
    Serial.print("updateGroupSlot:");
    Serial.println(body);
    root = validateGroupCreateBody(body);
  }
  if (!root && body != "") {
    // throw error bad body
    sendError(2, "groups/" + (slot + 1), "Bad JSON body");
    return true;
  }
  if (lightGroups[slot]) {
    delete lightGroups[slot];
    lightGroups[slot] = nullptr;
    if (SPIFFS.exists(fileName))
      SPIFFS.remove(fileName);
  }
  if (body != "") {
    Serial.print("Updating ");Serial.println(fileName);
    lightGroups[slot] = new LightGroup(root);
    File f = SPIFFS.open(fileName,"w");
    char* json = aJson.print(lightGroups[slot]->getJson());
    f.print(json);
    free(json);
    f.close();
    aJson.deleteItem(root);
  }
  return false;
}

void groupCreationHandler() {
  // handle group creation
  // find first available group slot
  int availableSlot = -1;
  for (int i = 0; i < 16; i++) {  // TEST start at 1 instead of 0
    if (!lightGroups[i]) {   
      availableSlot = i;
      break;
    }
  }
  if (availableSlot == -1) {
    // throw error no new groups allowed
    sendError(301, "groups", "Groups table full");
    return;
  }
  if (!updateGroupSlot(availableSlot, HTTP->arg("plain"))) {
    String slot = "";
    slot += (availableSlot + 1);
    sendSuccess("id", slot);
  }
}

aJsonObject *getGroupJson() {
  // iterate over groups and serialize
  aJsonObject *root = aJson.createObject();
  for (int i = 0; i < 16; i++) {
    if (lightGroups[i]) {
      String sIndex = "";
      sIndex += (i + 1);
      aJson.addItemToObject(root, sIndex.c_str(), lightGroups[i]->getJson());
    }
  }
  return root;
}

void groupListingHandler() {
  sendJson(getGroupJson());
}


int findSceneIndex(String id) {
  int index = -1;
  for (int i = 0; i < 16; i++) {
    LightGroup *scene = lightScenes[i];
    if (scene) {
      if (scene->id == id) {
        return i;
      }
    } else if (index == -1) {
      index = i;
    }
  }
  return index;
}

bool updateSceneSlot(int slot, String id, String body) {
  aJsonObject *root;
  if (body != "") {
    Serial.print("updateSceneSlot:");
    Serial.println(body);
    root = validateGroupCreateBody(body);
  }
  if (!root && body != "") {
    // throw error bad body
    sendError(2, "scenes/" + (slot + 1), "Bad JSON body");
    return true;
  }
  if (lightScenes[slot]) {
    String fileName = SCENE_FILE_TEMPLATE;
    fileName.replace("%d",String(slot));
    delete lightScenes[slot];
    lightScenes[slot] = nullptr;
    if(SPIFFS.exists(fileName)){
      SPIFFS.remove(fileName);   
    }
  }
  if (body != "") {
    lightScenes[slot] = new LightGroup(root);
    aJson.deleteItem(root);
  }
  return false;
}

String scenePutHandler(String id) {
  Serial.print("sceneCreationHandler()");Serial.println(id);
  int sceneIndex = findSceneIndex(id);
  // handle scene creation
  // find first available scene slot
  if (sceneIndex == -1) {
    // throw error no new scenes allowed
    sendError(301, "scenes", "Scenes table full");
    return "";
  }
  // updateSceneSlot sends failure messages
  if (!updateSceneSlot(sceneIndex, id, HTTP->arg("plain"))) {
      id = String(sceneIndex,DEC);
    // TODO - add file saves here
    Serial.print("updating lightScene->id to ");Serial.println(id);
    lightScenes[sceneIndex]->id = id;

    aJsonObject *root= aJson.createArray();
    aJsonObject *response1 = aJson.createArray();
    aJsonObject *response2 = aJson.createArray();
    aJsonObject *lights = aJson.createArray();
          for (int i = 0; i < 16; i++) {
        if (!((1 << i) & lightScenes[sceneIndex]->getLightMask())) {
          continue;
        }
        // add light to list
        String lightNum = "";
        lightNum += (i + 1);
        aJson.addItemToArray(lights, aJson.createItem(lightNum.c_str()));
      }
    aJsonObject *addr1 = aJson.createObject();
    aJsonObject *addr2 = aJson.createObject();
    aJsonObject *success1 = aJson.createObject();
    aJsonObject *success2 = aJson.createObject();
    char addressBuffer[30];
    sprintf(addressBuffer,"/scenes/%d/name",sceneIndex);
    aJson.addStringToObject(addr1, "address", addressBuffer);
    aJson.addStringToObject(addr1, "value", lightScenes[sceneIndex]->getName().c_str());
    sprintf(addressBuffer,"/scenes/%d/lights",sceneIndex);
    aJson.addStringToObject(addr2, "address", addressBuffer);
    aJson.addItemToObject(addr2, "value", lights);   
     
    aJson.addItemToObject(success1, "success", addr1);
    aJson.addItemToObject(success2, "success", addr2);

    aJson.addItemToArray(root, success1);
    aJson.addItemToArray(root, success2);
    sendJson(root);
    
    // now save to file
    String fileName = SCENE_FILE_TEMPLATE;
    fileName.replace("%d",String(sceneIndex));
    Serial.print("Updating Scene ");Serial.print(id);Serial.println(fileName);
    File f = SPIFFS.open(fileName,"w");
    char* json = aJson.print(lightScenes[sceneIndex]->getSceneJson(true));
    f.print(json);
    return id;
  }
}

void sceneCreationHandler(String id) {
  Serial.print("sceneCreationHandler()");Serial.println(id);
  int sceneIndex = findSceneIndex(id);
  // handle scene creation
  // find first available scene slot
  if (sceneIndex == -1) {
    // throw error no new scenes allowed
    sendError(301, "scenes", "Scenes table full");
    return;
  }
  // updateSceneSlot sends failure messages
  if (!updateSceneSlot(sceneIndex, id, HTTP->arg("plain"))) {
    //if (id == "") {
      id = String(sceneIndex,DEC);
    //}
    // TODO - add file saves here
    Serial.print("updating lightScene->id to ");Serial.println(id);
    lightScenes[sceneIndex]->id = id;
    sendSuccess("id", id);
    // now save to file
    String fileName = SCENE_FILE_TEMPLATE;
    fileName.replace("%d",String(sceneIndex));
    Serial.print("Updating Scene ");Serial.print(id);Serial.println(fileName);
    File f = SPIFFS.open(fileName,"w");
    char* json = aJson.print(lightScenes[sceneIndex]->getSceneJson(true));
    f.print(json);
    
    return;
  }
}

aJsonObject *getSceneJson() {
  // iterate over scenes and serialize
  aJsonObject *root = aJson.createObject();
  for (int i = 0; i < 16; i++) {
    if (lightScenes[i]) {
      Serial.print("Returning Scene :");Serial.println(lightScenes[i]->id.c_str());
      aJson.addItemToObject(root, lightScenes[i]->id.c_str(), lightScenes[i]->getSceneJson(true));
    }
  }
  return root;
}

void sceneListingHandler() {
  sendJson(getSceneJson());
}

LightGroup *findScene(String id) {
  for (int i = 0; i < 16; i++) {
    LightGroup *scene = lightScenes[i];
    if (scene) {
      if (scene->id == id) {
        return scene;
      }
    }
  }
  return nullptr;
}

String methodToString(int method) {
  switch (method) {
    case HTTP_POST: return "POST";
    case HTTP_GET: return "GET";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "unknown";
  }
}

