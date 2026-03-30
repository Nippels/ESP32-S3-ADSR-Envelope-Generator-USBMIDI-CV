//Log: 29.03.2026. ADSR wird sofort aus dem Webinterface aktualisiert. Trigger/Gate auf Pin 40 (V-Trigger DFAM Standard)

#include <Control_Surface.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

const char* ssid = "S3PS";
const char* password = "g8dncecc8.GxeXL";

USBMIDI_Interface midi;
WebServer server(80);
WebSocketsServer ws(81);

// LEDC PWM Einstellungen (neue API für ESP32 Core 3.x)
const int pwmPin = 39;
const int gatePin = 40;              // Gate/Trigger Ausgang
const int maxPWM = 16383;            // 2^14 - 1
const int pwmFreq = 2400;            // 2400 Hz
const int pwmRes = 14;               // 14 Bit Auflösung

// Gate/Trigger Einstellungen
const int gateHighVoltage = 3300;     // 3.3V für V-Trigger (DFAM Standard)
const int gateLowVoltage = 0;         // 0V

// ADSR Parameter
struct ADSRSettings {
    unsigned long attack;
    unsigned long decay;
    uint16_t sustain;
    unsigned long release;
};

ADSRSettings adsr = {500, 500, 8191, 500};

// Hüllkurven-Zustand
enum EnvelopeState { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
EnvelopeState currentState = IDLE;
unsigned long stateStartTime = 0;
uint16_t currentEnvelopeValue = 0;
bool noteActive = false;
uint8_t currentNote = 0;
float currentVelocity = 1.0;

// MIDI Event
uint8_t lastMidiType = 0;
uint8_t lastMidiNote = 0;
uint8_t lastMidiValue = 0;
bool midiEventAvailable = false;

// Timer
unsigned long lastEnvelopeUpdate = 0;
const int envelopeInterval = 5; // 5ms Update-Intervall

// Hilfsfunktion für korrekte Wertebereichs-Mapping
unsigned long mapValue(unsigned long x, unsigned long in_min, unsigned long in_max, unsigned long out_min, unsigned long out_max) {
    if (x <= in_min) return out_min;
    if (x >= in_max) return out_max;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

struct MyMIDI_Callbacks : FineGrainedMIDI_Callbacks<MyMIDI_Callbacks> {
    void onControlChange(Channel channel, uint8_t controller, uint8_t value, Cable cable) {
        lastMidiType = 1; // CC
        lastMidiNote = controller;
        lastMidiValue = value;
        midiEventAvailable = true;
        Serial.printf("CC: %d = %d\n", controller, value);
    }
    
    void onNoteOff(Channel channel, uint8_t note, uint8_t velocity, Cable cable) {
        lastMidiType = 2; // Note Off
        lastMidiNote = note;
        lastMidiValue = velocity;
        midiEventAvailable = true;
        Serial.printf("Note Off: %d\n", note);
    }
    
    void onNoteOn(Channel channel, uint8_t note, uint8_t velocity, Cable cable) {
        lastMidiType = 3; // Note On
        lastMidiNote = note;
        lastMidiValue = velocity;
        midiEventAvailable = true;
        Serial.printf("Note On: %d, Vel: %d\n", note, velocity);
    }
} callback;

// Gate/Trigger Steuerung
void setGate(bool high) {
    if (high) {
        digitalWrite(gatePin, HIGH);
        Serial.println("GATE: HIGH (3.3V)");
    } else {
        digitalWrite(gatePin, LOW);
        Serial.println("GATE: LOW (0V)");
    }
}

// ADSR Werte SOFORT aktualisieren mit korrekter Endwert-Behandlung
void updateADSRSettingsInstant(uint8_t controller, uint8_t value) {
    unsigned long newValue;
    
    if (controller == 1) { // Attack (1-2000ms)
        if (value == 0) {
            newValue = 1;
        } else if (value == 127) {
            newValue = 2000;
        } else {
            newValue = mapValue(value, 0, 127, 1, 2000);
        }
        
        if (adsr.attack != newValue) {
            adsr.attack = newValue;
            Serial.printf("Attack SOFORT aktualisiert = %d ms (MIDI: %d)\n", adsr.attack, value);
            
            if (currentState == ATTACK && noteActive) {
                unsigned long elapsed = millis() - stateStartTime;
                if (elapsed > adsr.attack) {
                    currentState = DECAY;
                    stateStartTime = millis();
                    Serial.println("Attack verkürzt -> sofort in DECAY");
                }
            }
        }
    }
    else if (controller == 2) { // Decay (1-2000ms)
        if (value == 0) {
            newValue = 1;
        } else if (value == 127) {
            newValue = 2000;
        } else {
            newValue = mapValue(value, 0, 127, 1, 2000);
        }
        
        if (adsr.decay != newValue) {
            adsr.decay = newValue;
            Serial.printf("Decay SOFORT aktualisiert = %d ms (MIDI: %d)\n", adsr.decay, value);
            
            if (currentState == DECAY && noteActive) {
                unsigned long elapsed = millis() - stateStartTime;
                if (elapsed > adsr.decay) {
                    currentState = SUSTAIN;
                    Serial.println("Decay verkürzt -> sofort in SUSTAIN");
                }
            }
        }
    }
    else if (controller == 3) { // Sustain (0-maxPWM)
        uint16_t newSustain;
        if (value == 0) {
            newSustain = 0;
        } else if (value == 127) {
            newSustain = maxPWM;
        } else {
            newSustain = mapValue(value, 0, 127, 0, maxPWM);
        }
        
        if (adsr.sustain != newSustain) {
            adsr.sustain = newSustain;
            Serial.printf("Sustain SOFORT aktualisiert = %d (MIDI: %d, %.2fV)\n", 
                         adsr.sustain, value, (adsr.sustain * 3.3 / maxPWM));
            
            if (currentState == SUSTAIN && noteActive) {
                currentEnvelopeValue = adsr.sustain;
                uint32_t pwmValue = (uint32_t)currentEnvelopeValue * currentVelocity;
                if (pwmValue > maxPWM) pwmValue = maxPWM;
                ledcWrite(pwmPin, (uint16_t)pwmValue);
                Serial.printf("Sustain sofort übernommen: PWM = %d (%.2fV)\n", 
                             (uint16_t)pwmValue, (pwmValue * 3.3 / maxPWM));
            }
        }
    }
    else if (controller == 4) { // Release (1-2000ms)
        if (value == 0) {
            newValue = 1;
        } else if (value == 127) {
            newValue = 2000;
        } else {
            newValue = mapValue(value, 0, 127, 1, 2000);
        }
        
        if (adsr.release != newValue) {
            adsr.release = newValue;
            Serial.printf("Release SOFORT aktualisiert = %d ms (MIDI: %d)\n", adsr.release, value);
            
            if (currentState == RELEASE && noteActive) {
                unsigned long elapsed = millis() - stateStartTime;
                if (elapsed > adsr.release) {
                    currentState = IDLE;
                    noteActive = false;
                    ledcWrite(pwmPin, 0);
                    setGate(false);  // Gate aus beim Ende des Release
                    Serial.println("Release verkürzt -> sofort aus");
                }
            }
        }
    }
}

// Diese Funktion wird nur noch für MIDI-CC verwendet
void updateADSRSettingsFromMIDI(uint8_t controller, uint8_t value) {
    updateADSRSettingsInstant(controller, value);
}

void updateEnvelope() {
    if (!noteActive && currentState != RELEASE) return;
    
    unsigned long now = millis();
    unsigned long elapsed = now - stateStartTime;
    uint16_t targetValue = currentEnvelopeValue;
    
    switch(currentState) {
        case ATTACK:
            if (elapsed >= adsr.attack) {
                targetValue = maxPWM;
                currentState = DECAY;
                stateStartTime = now;
                Serial.printf("-> DECAY (Value: %d, %.2fV)\n", targetValue, (targetValue * 3.3 / maxPWM));
            } else {
                targetValue = (uint16_t)((float)maxPWM * elapsed / adsr.attack);
            }
            break;
            
        case DECAY:
            if (elapsed >= adsr.decay) {
                targetValue = adsr.sustain;
                currentState = SUSTAIN;
                Serial.printf("-> SUSTAIN (Value: %d, %.2fV)\n", targetValue, (targetValue * 3.3 / maxPWM));
            } else {
                uint16_t range = maxPWM - adsr.sustain;
                targetValue = maxPWM - (uint16_t)((float)range * elapsed / adsr.decay);
            }
            break;
            
        case SUSTAIN:
            targetValue = adsr.sustain;
            break;
            
        case RELEASE:
            if (elapsed >= adsr.release) {
                targetValue = 0;
                currentState = IDLE;
                noteActive = false;
                setGate(false);  // Gate aus am Ende des Release
                Serial.println("-> IDLE - GATE OFF");
            } else {
                targetValue = adsr.sustain - (uint16_t)((float)adsr.sustain * elapsed / adsr.release);
            }
            break;
            
        default:
            return;
    }
    
    if (targetValue != currentEnvelopeValue) {
        currentEnvelopeValue = targetValue;
        
        // Velocity anwenden
        uint32_t pwmValue = (uint32_t)currentEnvelopeValue * currentVelocity;
        if (pwmValue > maxPWM) pwmValue = maxPWM;
        
        ledcWrite(pwmPin, (uint16_t)pwmValue);
        
        // Nur alle 100ms ausgeben für Debug
        static unsigned long lastDebug = 0;
        if (millis() - lastDebug > 100) {
            lastDebug = millis();
            Serial.printf("State: %d, PWM: %d (%.2fV)\n", currentState, (uint16_t)pwmValue, ((uint16_t)pwmValue * 3.3 / maxPWM));
        }
    }
}

void startNote(uint8_t note, uint8_t velocity) {
    currentNote = note;
    currentVelocity = velocity / 127.0;
    if (currentVelocity > 1.0) currentVelocity = 1.0;
    
    currentState = ATTACK;
    stateStartTime = millis();
    noteActive = true;
    
    // Gate einschalten bei Note On (V-Trigger: 3.3V)
    setGate(true);
    
    Serial.printf("\n=== NOTE ON: %d, Vel=%.2f ===\n", note, currentVelocity);
    Serial.printf("A=%dms D=%dms S=%d (%.2fV) R=%dms\n", 
                 adsr.attack, adsr.decay, adsr.sustain, (adsr.sustain * 3.3 / maxPWM), adsr.release);
    Serial.println("GATE: ON (V-Trigger 3.3V)");
}

void stopNote(uint8_t note) {
    if (note == currentNote && noteActive) {
        currentState = RELEASE;
        stateStartTime = millis();
        Serial.printf("=== NOTE OFF: %d ===\n", note);
        // Gate bleibt während Release an, wird am Ende des Release ausgeschaltet
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== MIDI to ADSR PWM with GATE ===");
    
    // LEDC PWM Setup für CV
    ledcAttach(pwmPin, pwmFreq, pwmRes);
    ledcWrite(pwmPin, 0);
    Serial.printf("CV Output on pin %d, freq=%d Hz, resolution=%d bit\n", pwmPin, pwmFreq, pwmRes);
    Serial.printf("Max PWM: %d entspricht %.2fV\n", maxPWM, (maxPWM * 3.3 / maxPWM));
    
    // Gate Setup
    pinMode(gatePin, OUTPUT);
    digitalWrite(gatePin, LOW);
    Serial.printf("GATE Output on pin %d (V-Trigger, DFAM Standard)\n", gatePin);
    Serial.println("GATE: HIGH = 3.3V, LOW = 0V");
    
    // WiFi
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 50) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi IP: %s\n", WiFi.localIP().toString().c_str());
    }
    
    // Web Interface mit GATE Anzeige
    server.on("/", []() {
        String html = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head><title>MIDI ADSR PWM with GATE</title>
        <style>
            body { background: black; color: #0f0; font-family: monospace; padding: 20px; }
            .control { margin: 10px 0; }
            input { width: 300px; background: #222; color: #0f0; border: 1px solid #0f0; cursor: pointer; }
            .value { display: inline-block; width: 40px; text-align: right; }
            h1 { color: #0f0; }
            .status { margin-top: 20px; padding: 10px; border: 1px solid #0f0; }
            .active-note { color: #ff0; }
            .gate-on { color: #0f0; font-weight: bold; }
            .gate-off { color: #888; }
            .voltage { color: #0ff; font-size: 0.9em; }
        </style>
        </head>
        <body>
            <h1>MIDI ADSR PWM + GATE (DFAM V-Trigger)</h1>
            <div class="control">Attack (CC1): <input type="range" id="a" min="0" max="127" value="63"> <span id="av" class="value">63</span></div>
            <div class="control">Decay (CC2): <input type="range" id="d" min="0" max="127" value="63"> <span id="dv" class="value">63</span></div>
            <div class="control">Sustain (CC3): <input type="range" id="s" min="0" max="127" value="64"> <span id="sv" class="value">64</span> <span class="voltage" id="sv_volt"></span></div>
            <div class="control">Release (CC4): <input type="range" id="r" min="0" max="127" value="63"> <span id="rv" class="value">63</span></div>
            <div class="status">
                <div>CV Output: Pin 39</div>
                <div>GATE Output: Pin 40 (V-Trigger)</div>
                <div>Status: <span id="status">Idle</span></div>
                <div>Gate: <span id="gateStatus" class="gate-off">OFF</span></div>
                <div>Aktuelle Note: <span id="currentNote" class="active-note">-</span></div>
            </div>
            <div id="log" style="margin-top: 20px; height: 200px; overflow-y: scroll;"></div>
            <script>
            let ws;
            
            function updateVoltage(value) {
                let volt = (value / 127) * 3.3;
                return volt.toFixed(2) + 'V';
            }
            
            function connect() {
                ws = new WebSocket('ws://' + location.hostname + ':81');
                ws.onmessage = (e) => {
                    let data = JSON.parse(e.data);
                    let log = document.getElementById('log');
                    log.innerHTML = '<div>[' + new Date().toLocaleTimeString() + '] ' + data.msg + '</div>' + log.innerHTML;
                    if (log.children.length > 20) log.removeChild(log.lastChild);
                    
                    if (data.state) {
                        document.getElementById('status').innerText = data.state;
                    }
                    if (data.gate !== undefined) {
                        let gateElem = document.getElementById('gateStatus');
                        if (data.gate) {
                            gateElem.innerText = 'ON (3.3V)';
                            gateElem.className = 'gate-on';
                        } else {
                            gateElem.innerText = 'OFF (0V)';
                            gateElem.className = 'gate-off';
                        }
                    }
                    if (data.note !== undefined) {
                        document.getElementById('currentNote').innerText = data.note;
                    }
                };
            }
            
            function send(cc, val) {
                if (ws && ws.readyState === WebSocket.OPEN) {
                    document.getElementById(cc === 1 ? 'av' : (cc === 2 ? 'dv' : (cc === 3 ? 'sv' : 'rv'))).innerText = val;
                    if (cc === 3) {
                        document.getElementById('sv_volt').innerText = updateVoltage(val);
                    }
                    ws.send(JSON.stringify({cc: cc, value: val}));
                }
            }
            
            document.getElementById('a').oninput = (e) => { send(1, parseInt(e.target.value)); };
            document.getElementById('d').oninput = (e) => { send(2, parseInt(e.target.value)); };
            document.getElementById('s').oninput = (e) => { send(3, parseInt(e.target.value)); };
            document.getElementById('r').oninput = (e) => { send(4, parseInt(e.target.value)); };
            
            document.getElementById('sv_volt').innerText = updateVoltage(64);
            connect();
            </script>
        </body>
        </html>
        )rawliteral";
        server.send(200, "text/html", html);
    });
    
    server.begin();
    ws.begin();
    
    // WebSocket Event-Handler
    ws.onEvent([](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
        if (type == WStype_TEXT) {
            String message = String((char*)payload);
            int ccIndex = message.indexOf("\"cc\"");
            int valIndex = message.indexOf("\"value\"");
            
            if (ccIndex > 0 && valIndex > 0) {
                int colon1 = message.indexOf(":", ccIndex);
                int comma1 = message.indexOf(",", colon1);
                String ccStr = message.substring(colon1 + 1, comma1);
                ccStr.trim();
                uint8_t controller = ccStr.toInt();
                
                int colon2 = message.indexOf(":", valIndex);
                int end2 = message.indexOf("}", colon2);
                String valStr = message.substring(colon2 + 1, end2);
                valStr.trim();
                uint8_t value = valStr.toInt();
                
                updateADSRSettingsInstant(controller, value);
                
                String response = "{\"msg\":\"ADSR sofort: CC" + String(controller) + " = " + String(value) + "\"}";
                ws.sendTXT(num, response);
            }
        }
    });
    
    midi.setCallbacks(callback);
    midi.begin();
    
    Serial.println("\nReady! Send MIDI notes and CC1-4");
    Serial.println("ADSR-Werte werden SOFORT nach WebUI-Änderung übernommen!");
    Serial.println("Gate: HIGH bei Note On, LOW am Ende des Release");
    Serial.println("V-Trigger: 3.3V = ON, 0V = OFF (DFAM Standard)");
}

void loop() {
    ws.loop();
    server.handleClient();
    midi.update();
    
    if (millis() - lastEnvelopeUpdate >= envelopeInterval) {
        lastEnvelopeUpdate = millis();
        updateEnvelope();
    }
    
    if (midiEventAvailable) {
        midiEventAvailable = false;
        
        String wsMsg;
        String stateStr;
        
        if (lastMidiType == 1) {
            updateADSRSettingsFromMIDI(lastMidiNote, lastMidiValue);
            wsMsg = "{\"msg\":\"MIDI-CC" + String(lastMidiNote) + " = " + String(lastMidiValue) + "\"}";
        }
        else if (lastMidiType == 2) {
            stopNote(lastMidiNote);
            wsMsg = "{\"msg\":\"Note Off: " + String(lastMidiNote) + "\", \"state\":\"Release\", \"gate\":false, \"note\":\"-\"}";
        }
        else if (lastMidiType == 3) {
            if (lastMidiValue > 0) {
                startNote(lastMidiNote, lastMidiValue);
                
                if (currentState == ATTACK) stateStr = "Attack";
                else if (currentState == DECAY) stateStr = "Decay";
                else stateStr = "Sustain";
                
                wsMsg = "{\"msg\":\"Note On: " + String(lastMidiNote) + " Vel:" + String(lastMidiValue) + "\", \"state\":\"" + stateStr + "\", \"gate\":true, \"note\":\"" + String(lastMidiNote) + "\"}";
            } else {
                stopNote(lastMidiNote);
                wsMsg = "{\"msg\":\"Note Off: " + String(lastMidiNote) + "\", \"state\":\"Release\", \"gate\":false, \"note\":\"-\"}";
            }
        }
        
        ws.broadcastTXT(wsMsg);
    }
}
