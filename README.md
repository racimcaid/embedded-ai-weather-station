# Embedded AI Weather Station

![Arduino](https://img.shields.io/badge/Arduino-GIGA%20R1%20WiFi-00979D?logo=arduino&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-Embedded-blue)
![TensorFlow Lite](https://img.shields.io/badge/TensorFlow%20Lite-Embedded%20Inference-orange)
![Machine Learning](https://img.shields.io/badge/ML-Weather%20Prediction-brightgreen)
![Status](https://img.shields.io/badge/Status-Prototype-informational)

An end-to-end embedded weather station built on **Arduino GIGA R1 WiFi**, combining **real-time environmental sensing**, an **on-device machine learning model** for **temperature forecasting (+1 hour)**, and an **embedded web dashboard** for live monitoring.

---

## Overview

This project was developed as an embedded systems and AI integration project to demonstrate the complete pipeline from **data-driven model design** to **deployment on a microcontroller**.

The system:
- acquires real-time weather data from multiple sensors,
- estimates wind speed, wind direction, and rainfall,
- runs a **local AI inference pipeline** on the embedded target,
- predicts the **next-hour temperature**,
- exposes all measurements through a **WiFi-hosted dashboard and JSON endpoint**.

The objective was not only to build a weather station, but to design a **full embedded intelligent system** integrating:
- hardware interfacing,
- signal acquisition,
- non-blocking firmware scheduling,
- machine learning preprocessing,
- TensorFlow Lite conversion,
- embedded inference,
- and lightweight web visualization.

---

## Project Preview

### Embedded web dashboard
![Dashboard](docs/images/dashboard-web-interface.png)

### Hardware prototype
![Hardware setup](docs/images/hardware-setup-full.jpg)

---

## Key Features

- **Arduino GIGA R1 WiFi** based embedded platform
- **BME680** environmental sensor for:
  - temperature
  - humidity
  - pressure
  - gas resistance
- Weather meter kit integration for:
  - **anemometer**
  - **wind vane**
  - **tipping bucket rain gauge**
- **Embedded web server** with:
  - live dashboard
  - real-time charts
  - JSON API endpoint
- **On-device AI inference**
- **Temperature prediction at +1h**
- End-to-end pipeline from:
  - training notebook
  - model export
  - TensorFlow Lite conversion
  - C header generation
  - deployment on Arduino

---

## System Architecture

The project is organized as a complete embedded AI pipeline:

1. **Sensor acquisition**
   - BME680 over I2C for environmental data
   - wind and rain sensors through digital interrupts and analog measurement

2. **Signal processing**
   - pulse counting for wind speed
   - resistance-based lookup for wind direction
   - tipping bucket accumulation for rainfall

3. **Historical feature construction**
   - sliding window of the last 6 measurement steps
   - features built from:
     - temperature
     - humidity
     - pressure

4. **Time feature encoding**
   - cyclic encoding of:
     - hour
     - month
   - using sine/cosine representation

5. **Embedded AI inference**
   - normalized feature vector
   - TensorFlow Lite model executed locally through **ArduTFLite**
   - next-hour temperature prediction

6. **Data serving**
   - JSON endpoint on `/data`
   - HTML dashboard on `/`

---

## Hardware Stack

### Main board
- **Arduino GIGA R1 WiFi**

### Sensors
- **BME680** environmental sensor
- Weather meter kit including:
  - **cup anemometer**
  - **wind vane**
  - **tipping bucket rain gauge**

### Measured variables
- temperature
- humidity
- pressure
- gas resistance
- wind speed
- wind direction
- rainfall

---

## Machine Learning Pipeline

The AI part of the project was first developed and validated offline in Jupyter notebooks, then exported to the embedded platform.

### Dataset
The model was trained using the **Guangzhou subset** of the **PM2.5 Data of Five Chinese Cities** dataset from the **UCI Machine Learning Repository**.

### Prediction target
The embedded model predicts:

- **temperature at t+1 hour**

### Selected input variables
Only the most relevant variables for the embedded use case were retained:
- **TEMP**
- **HUMI**
- **PRES**

### Feature engineering
A supervised learning dataset was built using a **6-step sliding window**.

The final input vector contains **22 features**:
- **18 historical features**
  - 6 temperature values
  - 6 humidity values
  - 6 pressure values
- **4 time features**
  - hour_sin
  - hour_cos
  - month_sin
  - month_cos

### Preprocessing
- chronological sorting by timestamp
- missing value removal
- feature standardization with **StandardScaler**
- chronological train / validation / test split:
  - **70% train**
  - **15% validation**
  - **15% test**

### Model architecture
A compact **MLP** was selected for embedded deployment:

- input: **22 features**
- hidden layer 1: **Dense(32, ReLU)**
- hidden layer 2: **Dense(16, ReLU)**
- output: **Dense(1)**

### Training strategy
- optimizer: **Adam**
- loss: **MSE**
- metric: **MAE**
- **EarlyStopping** with best weights restoration

---

## Model Conversion and Embedded Deployment

After training, the Keras model was converted to **TensorFlow Lite** and then exported as a C header for microcontroller integration.

Deployment workflow:
1. train the model in TensorFlow / Keras
2. export the trained `.keras` model
3. convert it to `.tflite`
4. convert the `.tflite` binary into `model_data.h`
5. load the model in the Arduino firmware using **ArduTFLite**
6. run inference directly on the board

This approach allows the system to perform **fully local prediction**, without any cloud dependency.

---

## Results

### Test performance
The trained MLP achieved:

- **Test MAE:** `0.4257 °C`
- **Test RMSE:** `0.6706 °C`

### Baseline comparison
A naive baseline using the current temperature as the next-hour prediction gave:

- **Baseline MAE:** `0.6274 °C`
- **Baseline RMSE:** `0.9465 °C`

The MLP therefore improves significantly over a simple persistence-based baseline.

### Functional prototype results
The embedded prototype successfully demonstrates:
- real-time acquisition of environmental data
- wind and rain event counting
- live web dashboard visualization
- on-device AI prediction
- end-to-end integration between sensing, inference, and web serving

---

## Embedded Firmware Highlights

The firmware includes:
- non-blocking task scheduling using `millis()`
- interrupt-based wind and rain counting
- BME680 acquisition over I2C
- wind direction estimation through resistance lookup
- AI history buffer construction
- feature normalization on-device
- embedded TensorFlow Lite inference
- JSON serialization for web communication
- HTML + Chart.js dashboard generation directly from firmware

---

## Web Interface

The board hosts a lightweight web interface that displays:
- temperature
- humidity
- pressure
- wind speed
- rainfall
- wind direction
- AI prediction at +1h
- AI history buffer status

The dashboard also includes real-time charts for:
- temperature
- humidity
- pressure
- wind speed

---

## Repository Structure

```text
embedded-ai-weather-station/
├── README.md
├── LICENSE
├── .gitignore
├── docs/
│   └── images/
│       ├── dashboard-web-interface.png
│       ├── serial-monitor-startup.png
│       ├── hardware-setup-close.jpg
│       └── hardware-setup-full.jpg
├── hardware/
│   └── sensors/
│       └── weather-meter-kit-datasheet.pdf
├── software/
│   ├── arduino/
│   │   └── weather_station_giga/
│   │       ├── weather_station_giga.ino
│   │       └── model_data.h
│   └── ai/
│       ├── training/
│       │   └── weather_prediction_training.ipynb
│       ├── conversion/
│       │   └── tf_to_tflite.ipynb
│       └── exported-model/
│           └── weather_prediction_model.tflite
└── results/
    └── demo-video/
        └── demo.mp4
## Getting Started

### 1. Hardware setup

Connect:
- the **BME680** to the Arduino GIGA R1 WiFi through I2C
- the weather meter kit sensors to the corresponding analog/digital pins used in the firmware

### 2. Install Arduino dependencies

Install the required libraries:
- `Adafruit_BME680`
- `Adafruit_Sensor`
- `ArduTFLite`

### 3. Configure WiFi

In the Arduino sketch, replace:

```cpp
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";
with your local WiFi credentials.

### 4. Upload the firmware

Open the Arduino project and upload the firmware to the Arduino GIGA R1 WiFi.

### 5. Access the dashboard

Once connected to WiFi, open the board IP address in your browser.

## Prototype Notes

This repository reflects a **working prototype** and also documents the current engineering trade-offs.

### Current prototype assumptions

- `HISTORY_INTERVAL_MS = 10000UL` is used for **demonstration purposes**
  - this accelerates validation of the AI pipeline
  - in a real hourly deployment, this interval should be set to **1 hour**
- `CURRENT_HOUR` and `CURRENT_MONTH` are **manually fixed**
  - future versions should retrieve real time from **RTC** or **NTP**
- the current setup is a **bench prototype**
  - not yet an outdoor ruggedized system
- WiFi credentials are intentionally left as placeholders in the public repository

This section is intentionally transparent: the goal of this repository is to present both the **validated functionalities** and the **next engineering steps**.

---

## Future Improvements

Planned next steps include:

- replacing fixed time features with real-time clock or NTP synchronization
- using true hourly historical sampling in deployment mode
- improving enclosure and field robustness
- adding persistent local storage for measurements
- extending prediction targets to additional weather variables
- evaluating lightweight recurrent or temporal models for comparison
- improving calibration and long-term outdoor reliability

---

## Why This Project Matters

This project showcases the ability to design and integrate a complete embedded system combining:

- sensor interfacing
- microcontroller firmware development
- real-time data acquisition
- web-based embedded visualization
- machine learning preprocessing
- model deployment on constrained hardware
- end-to-end embedded AI engineering

It is intended as a portfolio project for **embedded systems**, **edge AI**, and **intelligent connected devices** roles.

---

## Author

**Racim Caid**  
Master’s student in **Complex Systems Engineering**  
Focused on **embedded systems**, **edge AI**, and intelligent electronic systems.

---

## Credits

- **UCI Machine Learning Repository** — *PM2.5 Data of Five Chinese Cities*
- **Arduino**
- **Adafruit**
- **TensorFlow / TensorFlow Lite**
- Weather meter kit datasheet included in this repository

---

## License

This project is distributed under the **MIT License**.
