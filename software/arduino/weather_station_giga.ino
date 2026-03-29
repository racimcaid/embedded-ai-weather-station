#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Arduino.h>
#include <math.h>
#include <ArduTFLite.h>

#include "model_data.h"

// =====================================================
// WIFI CONFIGURATION
// Replace these placeholders with local network credentials
// before deploying the firmware.
// =====================================================
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// =====================================================
// EMBEDDED WEB SERVER
// Provides:
// - a real-time HTML dashboard on "/"
// - a JSON API endpoint on "/data"
// =====================================================
WiFiServer server(80);

// =====================================================
// BME680 ENVIRONMENTAL SENSOR
// Arduino GIGA R1 WiFi default I2C pins:
// SDA = D20, SCL = D21
// =====================================================
Adafruit_BME680 bme;
bool bmeDetected = false;
uint8_t bmeAddr = 0;

// Latest validated environmental measurements
float lastTemp = 0.0;
float lastHum = 0.0;
float lastPres = 0.0;
float lastGas = 0.0;
bool lastValid = false;
unsigned long lastTimeMs = 0;

// =====================================================
// WIND SENSORS
// - Anemometer on A2
// - Wind vane on A0
// =====================================================
const int pinAnemometer = A2;
const int pinWindVane = A0;

volatile int anemometerCount = 0;
float windSpeed = 0.0;
float windDir = 0.0;
unsigned long lastWindTime = 0;

// =====================================================
// RAIN GAUGE
// Tipping bucket rain sensor connected to digital pin 5
// =====================================================
const int pinRain = 5;
volatile int rainCount = 0;      // total number of bucket tips
float rainLevel = 0.0;           // cumulative rainfall in mm
unsigned long lastRainTime = 0;  // debounce protection

// =====================================================
// TASK SCHEDULING
// Non-blocking timing management for acquisition,
// history update, and inference.
// =====================================================
unsigned long lastBmeUpdateMs = 0;
unsigned long lastWindUpdateMs = 0;
unsigned long lastHistoryPushMs = 0;

// Acquisition intervals
const unsigned long BME_INTERVAL_MS = 2000UL;   // BME680 read every 2 s
const unsigned long WIND_INTERVAL_MS = 2000UL;  // wind/rain update every 2 s

// Prototype / demo mode:
// history is updated every 10 seconds to validate the
// end-to-end AI pipeline quickly.
//
// For real hourly prediction deployment, replace this
// value with 3600000UL (1 hour).
const unsigned long HISTORY_INTERVAL_MS = 10000UL;

// =====================================================
// AI PREDICTION CONFIGURATION
// The embedded model uses a 6-step history window.
// =====================================================
const int HISTORY_LEN = 6;
const int N_FEATURES = 22;

float tempHist[HISTORY_LEN] = {0};
float humHist[HISTORY_LEN] = {0};
float presHist[HISTORY_LEN] = {0};

int historyCount = 0;
bool predictionReady = false;
float predTempNextHour = 0.0;

// =====================================================
// TENSOR ARENA
// Memory buffer used by the TensorFlow Lite Micro runtime
// =====================================================
constexpr int kTensorArenaSize = 16 * 1024;
byte tensorArena[kTensorArenaSize];
bool modelReady = false;

// =====================================================
// FEATURE NORMALIZATION PARAMETERS
// Exported from the training pipeline and reused on-device
// to ensure consistency between training and inference.
// =====================================================
const float scalerMean[N_FEATURES] = {
21.53750510f, 75.22413930f, 1005.79252000f,
21.53749690f, 75.22384040f, 1005.79258000f,
21.53757030f, 75.22316120f, 1005.79261000f,
21.53767360f, 75.22218300f, 1005.79262000f,
21.53779040f, 75.22112330f, 1005.79261000f,
21.53791270f, 75.22000920f, 1005.79259000f,
0.00005342f, -0.00019936f, 0.01389015f, 0.03638227f
};

const float scalerScale[N_FEATURES] = {
6.96240143f, 106.47348954f, 6.92927377f,
6.96241101f, 106.47350280f, 6.92935906f,
6.96233527f, 106.47353294f, 6.92940201f,
6.96222942f, 106.47359010f, 6.92942438f,
6.96210544f, 106.47364705f, 6.92941040f,
6.96197393f, 106.47369644f, 6.92938719f,
0.70709846f, 0.70711507f, 0.69805996f, 0.71497950f
};

// =====================================================
// TIME FEATURES USED BY THE MODEL
// Prototype version:
// CURRENT_HOUR and CURRENT_MONTH are manually fixed.
//
// In a production-ready version, these values should be
// retrieved automatically from an RTC module or NTP sync.
// =====================================================
int CURRENT_HOUR = 21;
int CURRENT_MONTH = 3;

// =====================================================
// WIND VANE RESISTANCE LOOKUP TABLE
// Maps measured resistance to wind direction angle.
// Based on the weather meter kit datasheet.
// =====================================================
struct WindAngle {
float resistance;
float angle;
};

WindAngle mapTable[] = {
{33000, 0}, {6570, 22.5}, {8200, 45}, {891, 67.5},
{1000, 90}, {688, 112.5}, {2200, 135}, {1410, 157.5},
{3900, 180}, {3140, 202.5}, {16000, 225}, {14120, 247.5},
{120000, 270},{42120, 292.5}, {64900, 315}, {21880, 337.5}
};

// =====================================================
// INTERRUPT SERVICE ROUTINES
// Used to count wind and rain events in real time.
// =====================================================
void anemometerISR() {
anemometerCount++;
}

void rainISR() {
if (millis() - lastRainTime > 50) {
rainCount++;
lastRainTime = millis();
}
}

// =====================================================
// UTILITY FUNCTIONS
// =====================================================
float voltageToDegrees(float voltage) {
float Vcc = 3.3;
float R_ext = 10000.0;

// Protection against invalid divider values
if (Vcc - voltage < 0.1) return 0;

float R_sensor = R_ext * voltage / (Vcc - voltage);

float closestAngle = 0;
float minDiff = 1e6;
for (int i = 0; i < 16; i++) {
float diff = abs(R_sensor - mapTable[i].resistance);
if (diff < minDiff) {
minDiff = diff;
closestAngle = mapTable[i].angle;
}
}
return closestAngle;
}

// =====================================================
// CONTINUOUS BME680 ACQUISITION
// Updates the latest validated environmental readings.
// =====================================================
void updateBME680() {
if (bmeDetected && bme.performReading()) {
lastTemp = bme.temperature;
lastHum = bme.humidity;
lastPres = bme.pressure / 100.0;
lastGas = bme.gas_resistance;
lastValid = true;
lastTimeMs = millis();
}
}

// =====================================================
// CONTINUOUS WIND / RAIN ACQUISITION
// - wind speed from pulse count
// - wind direction from analog voltage
// - rainfall from tipping bucket counter
// =====================================================
void updateWindRain() {
unsigned long now = millis();
float dt = (now - lastWindTime) / 1000.0;

if (dt < 2.0) return;

int localAnemometerCount;
int localRainCount;

noInterrupts();
localAnemometerCount = anemometerCount;
anemometerCount = 0;
localRainCount = rainCount;
interrupts();

// Wind speed in km/h
windSpeed = (localAnemometerCount / dt) * 2.4;

// Wind direction in degrees
int raw = analogRead(pinWindVane);
float voltage = raw * (3.3 / 1023.0);
windDir = voltageToDegrees(voltage);

// Cumulative rainfall in mm
rainLevel = localRainCount * 0.2794;

lastWindTime = now;
}

// =====================================================
// HISTORY BUFFER FOR AI INFERENCE
// Stores the latest 6 measurement steps used as model input.
// =====================================================
void pushHistoryPoint() {
if (!lastValid) return;

for (int i = 0; i < HISTORY_LEN - 1; i++) {
tempHist[i] = tempHist[i + 1];
humHist[i] = humHist[i + 1];
presHist[i] = presHist[i + 1];
}

tempHist[HISTORY_LEN - 1] = lastTemp;
humHist[HISTORY_LEN - 1] = lastHum;
presHist[HISTORY_LEN - 1] = lastPres;

if (historyCount < HISTORY_LEN) {
historyCount++;
}

if (historyCount >= HISTORY_LEN) {
predictionReady = true;
}
}

// =====================================================
// MODEL FEATURE VECTOR CONSTRUCTION
// 22 input features:
// - 18 historical values (temperature, humidity, pressure)
// - 4 cyclic time features for t+1 (hour/month)
// =====================================================
void buildFeatureVector(float features[N_FEATURES]) {
// 18 historical features
features[0] = tempHist[0];
features[1] = humHist[0];
features[2] = presHist[0];

features[3] = tempHist[1];
features[4] = humHist[1];
features[5] = presHist[1];

features[6] = tempHist[2];
features[7] = humHist[2];
features[8] = presHist[2];

features[9] = tempHist[3];
features[10] = humHist[3];
features[11] = presHist[3];

features[12] = tempHist[4];
features[13] = humHist[4];
features[14] = presHist[4];

features[15] = tempHist[5];
features[16] = humHist[5];
features[17] = presHist[5];

// 4 time features for t+1
int nextHour = (CURRENT_HOUR + 1) % 24;
int month = CURRENT_MONTH;

float hourAngle = 2.0f * PI * ((float)nextHour / 24.0f);
float monthAngle = 2.0f * PI * ((float)(month - 1) / 12.0f);

features[18] = sin(hourAngle);
features[19] = cos(hourAngle);
features[20] = sin(monthAngle);
features[21] = cos(monthAngle);
}

// Standardization using training-time statistics
void normalizeFeatures(float features[N_FEATURES]) {
for (int i = 0; i < N_FEATURES; i++) {
features[i] = (features[i] - scalerMean[i]) / scalerScale[i];
}
}

// =====================================================
// EMBEDDED AI MODEL
// =====================================================
void initPredictionModel() {
modelReady = modelInit(model_data, tensorArena, kTensorArenaSize);

if (modelReady) {
Serial.println("Modèle IA prêt.");
} else {
Serial.println("Erreur: initialisation du modèle IA échouée.");
}
}

void runPredictionModel() {
if (!predictionReady) return;
if (!modelReady) return;

float features[N_FEATURES];
buildFeatureVector(features);
normalizeFeatures(features);

// Copy normalized features into the model input tensor
for (int i = 0; i < N_FEATURES; i++) {
if (!modelSetInput(features[i], i)) {
Serial.print("Erreur input index ");
Serial.println(i);
return;
}
}

// Run inference on-device
if (!modelRunInference()) {
Serial.println("Erreur: inference échouée.");
return;
}

// Output: predicted temperature at t+1
predTempNextHour = modelGetOutput(0);

Serial.print("Prediction +1h = ");
Serial.println(predTempNextHour, 4);
}

// =====================================================
// JSON RESPONSE GENERATION
// Used by the embedded web dashboard.
// =====================================================
String makeJSON() {
if (!lastValid) return "{\"ok\":false}";

String s = "{";
s += "\"ok\":true,";
s += "\"temperature\":" + String(lastTemp, 2) + ",";
s += "\"humidity\":" + String(lastHum, 2) + ",";
s += "\"pressure\":" + String(lastPres, 2) + ",";
s += "\"gas\":" + String(lastGas, 0) + ",";
s += "\"windSpeed\":" + String(windSpeed, 1) + ",";
s += "\"windDir\":" + String(windDir, 0) + ",";
s += "\"rain\":" + String(rainLevel, 2) + ",";
s += "\"predictionReady\":" + String(predictionReady ? "true" : "false") + ",";
s += "\"predTempNextHour\":" + String(predTempNextHour, 2) + ",";
s += "\"historyCount\":" + String(historyCount) + ",";
s += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
s += "\"ts\":" + String(lastTimeMs);
s += "}";
return s;
}

// =====================================================
// JSON HTTP RESPONSE
// =====================================================
void sendJSON(WiFiClient &client) {
String j = makeJSON();
client.println("HTTP/1.1 200 OK");
client.println("Content-Type: application/json; charset=utf-8");
client.println("Connection: close");
client.println();
client.println(j);
}

// =====================================================
// EMBEDDED HTML DASHBOARD
// Real-time visualization of measurements and AI prediction.
// =====================================================
void sendHTML(WiFiClient &client) {
client.println("HTTP/1.1 200 OK");
client.println("Content-Type: text/html; charset=utf-8");
client.println("Connection: close");
client.println();

client.println(R"rawliteral(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Station Météo 🌤️</title>

<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

<style>
body{font-family:Arial,sans-serif;background:#f0f8ff;padding:20px;margin:0;}
h1{text-align:center;color:#052c38;margin-bottom:10px;}
.datetime{text-align:center;color:#6b7280;margin-bottom:20px;font-size:16px;}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px;margin-bottom:30px;}
.card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.08);display:flex;flex-direction:column;align-items:center;}
.icon{font-size:28px;margin-bottom:6px;}
.label{font-size:14px;color:#6b7280;}
.value{font-size:28px;font-weight:bold;color:#052c38;}
.wind-arrow{font-size:24px;transition:transform 0.5s;}
canvas{background:#fff;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.08);margin-bottom:20px;padding:8px;}

.graph-grid{
display:grid;
grid-template-columns:1fr 1fr;
gap:20px;
margin-top:20px;
}

.graph-grid canvas{
width:100% !important;
height:220px !important;
}

@media (max-width:900px){
.graph-grid{
grid-template-columns:1fr;
}
}
</style>
</head>

<body>

<h1>🌤️ Station Météo 🌡️💧📈💨🧭</h1>
<div class="datetime" id="datetime">--</div>

<div class="grid">
<div class="card"><div class="icon">🌡️</div><div class="label">Température</div><div id="tVal" class="value">-- °C</div></div>
<div class="card"><div class="icon">💧</div><div class="label">Humidité</div><div id="hVal" class="value">-- %</div></div>
<div class="card"><div class="icon">📈</div><div class="label">Pression</div><div id="pVal" class="value">-- hPa</div></div>
<div class="card"><div class="icon">💨</div><div class="label">Vent (km/h)</div><div id="wVal" class="value">--</div></div>
<div class="card"><div class="icon">🌧️</div><div class="label">Pluie (mm)</div><div id="rVal" class="value">-- mm</div></div>
<div class="card"><div class="icon">🧭</div><div class="label">Direction vent</div>
<div id="windArrow" class="wind-arrow">↑</div>
<div id="wdVal" class="value">-- °</div>
</div>
<div class="card"><div class="icon">🤖</div><div class="label">Prédiction +1h</div><div id="predVal" class="value">Init...</div></div>
<div class="card"><div class="icon">🗂️</div><div class="label">Historique IA</div><div id="histVal" class="value">0/6</div></div>
</div>

<h2>Graphiques en temps réel</h2>

<div class="graph-grid">
<canvas id="tempChart" height="120"></canvas>
<canvas id="humChart" height="120"></canvas>
<canvas id="presChart" height="120"></canvas>
<canvas id="windChart" height="120"></canvas>
</div>

<script>
function updateDateTime(){
const now=new Date();
document.getElementById('datetime').textContent =
now.toLocaleDateString()+' '+now.toLocaleTimeString();
}
setInterval(updateDateTime,1000);
updateDateTime();

function makeChart(id,label,color){
return new Chart(document.getElementById(id),{
type:'line',
data:{
labels:[],
datasets:[{
label:label,
data:[],
borderColor:color,
fill:false,
tension:0.3
}]
},
options:{
responsive:true,
maintainAspectRatio:false,
animation:false,
plugins:{
legend:{
labels:{ font:{ size:18 } }
}
},
scales:{
x:{ display:false },
y:{ ticks:{ font:{ size:16 } } }
}
}
});
}

const tempChart = makeChart('tempChart','Température °C','red');
const humChart = makeChart('humChart','Humidité %','blue');
const presChart = makeChart('presChart','Pression hPa','green');
const windChart = makeChart('windChart','Vent km/h','orange');

function pushData(chart,label,value){
chart.data.labels.push(label);
chart.data.datasets[0].data.push(value);
if(chart.data.labels.length>20){
chart.data.labels.shift();
chart.data.datasets[0].data.shift();
}
chart.update();
}

async function update(){
try{
const res = await fetch('/data');
const j = await res.json();
if(!j.ok) return;

const t = new Date().toLocaleTimeString();

document.getElementById('tVal').textContent = j.temperature.toFixed(2)+' °C';
document.getElementById('hVal').textContent = j.humidity.toFixed(2)+' %';
document.getElementById('pVal').textContent = j.pressure.toFixed(2)+' hPa';
document.getElementById('wVal').textContent = j.windSpeed.toFixed(1);
document.getElementById('rVal').textContent = j.rain.toFixed(2)+' mm';
document.getElementById('wdVal').textContent = j.windDir.toFixed(0)+' °';
document.getElementById('windArrow').style.transform = 'rotate('+j.windDir+'deg)';

document.getElementById('histVal').textContent = j.historyCount + '/6';

if(j.predictionReady){
document.getElementById('predVal').textContent = j.predTempNextHour.toFixed(2)+' °C';
} else {
document.getElementById('predVal').textContent = 'Init...';
}

pushData(tempChart,t,j.temperature);
pushData(humChart,t,j.humidity);
pushData(presChart,t,j.pressure);
pushData(windChart,t,j.windSpeed);

}catch(e){
console.log(e);
}
setTimeout(update,2000);
}
update();
</script>

</body>
</html>
)rawliteral");
}

// =====================================================
// HTTP CLIENT HANDLING
// Routes requests either to the JSON API or to the HTML page.
// =====================================================
void handleClient() {
WiFiClient client = server.accept();
if (!client) return;

String req = client.readStringUntil('\n');
req.trim();

while (client.available()) {
client.readStringUntil('\n');
}

if (req.startsWith("GET /data")) {
sendJSON(client);
} else {
sendHTML(client);
}

client.stop();
}

// =====================================================
// SETUP
// Initializes sensors, interrupts, WiFi, web server,
// AI model, and scheduler timestamps.
// =====================================================
void setup() {
Serial.begin(115200);
// while (!Serial);

// Keep 10-bit ADC resolution to preserve the original
// wind vane conversion logic.
analogReadResolution(10);

// Initialize I2C bus on the Arduino GIGA R1 WiFi
Wire.begin();

// --- BME680 setup ---
const uint8_t addrs[] = {0x76, 0x77};
for (uint8_t i = 0; i < 2; i++) {
uint8_t addr = addrs[i];
if (bme.begin(addr)) {
bmeDetected = true;
bmeAddr = addr;
break;
}
}

if (bmeDetected) {
bme.setTemperatureOversampling(BME680_OS_8X);
bme.setHumidityOversampling(BME680_OS_2X);
bme.setPressureOversampling(BME680_OS_4X);
bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
bme.setGasHeater(0, 0);
} else {
Serial.println("BME680 non détecté ! Vérifiez le câblage.");
}

// --- Wind sensor setup ---
pinMode(pinAnemometer, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(pinAnemometer), anemometerISR, FALLING);
pinMode(pinWindVane, INPUT);

// --- Rain gauge setup ---
pinMode(pinRain, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(pinRain), rainISR, FALLING);
rainCount = 0;
lastRainTime = millis();

// --- WiFi setup ---
Serial.print("Connexion WiFi à ");
Serial.println(ssid);

int status = WiFi.begin(ssid, pass);
unsigned long t0 = millis();

while (status != WL_CONNECTED) {
delay(500);
if (millis() - t0 > 15000) {
t0 = millis();
status = WiFi.begin(ssid, pass);
Serial.println("Nouvelle tentative...");
} else {
status = WiFi.status();
}
}

Serial.print("Connecté ! IP: ");
Serial.println(WiFi.localIP());

server.begin();
initPredictionModel();

// Initialize scheduler timestamps
unsigned long now = millis();
lastBmeUpdateMs = now;
lastWindUpdateMs = now;
lastHistoryPushMs = now;
lastWindTime = now;
}

// =====================================================
// MAIN LOOP
// Periodically:
// 1. reads BME680 data
// 2. updates wind/rain measurements
// 3. updates AI history and runs inference
// 4. handles incoming HTTP clients
// =====================================================
void loop() {
unsigned long now = millis();

// 1) Continuous BME680 acquisition
if (now - lastBmeUpdateMs >= BME_INTERVAL_MS) {
lastBmeUpdateMs = now;
updateBME680();
}

// 2) Continuous wind/rain update
if (now - lastWindUpdateMs >= WIND_INTERVAL_MS) {
lastWindUpdateMs = now;
updateWindRain();
}

// 3) History update + AI inference
if (now - lastHistoryPushMs >= HISTORY_INTERVAL_MS) {
lastHistoryPushMs = now;
pushHistoryPoint();
runPredictionModel();
}

// 4) Embedded web server
handleClient();
}
