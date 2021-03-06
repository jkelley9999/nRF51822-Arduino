/*
    Copyright (c) 2014 RedBearLab, All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <Servo.h>
#include <BLE_API.h>

#define TXRX_BUF_LEN                     20

//Pin For Nano
#ifdef BLE_NANO

#define DIGITAL_OUT_PIN   		 D1
#define DIGITAL_IN_PIN     		 D2
#define PWM_PIN           	         D0
#define SERVO_PIN        		 D3
#define ANALOG_IN_PIN      		 A3

#endif

#define STATUS_CHECK_TIME                APP_TIMER_TICKS(200, 0)

Servo myservo;

BLEDevice  ble;

static app_timer_id_t                    m_status_check_id; 
static boolean analog_enabled = false;
static byte old_state = LOW;
// The Nordic UART Service
static const uint8_t uart_base_uuid[]     = {0x71, 0x3D, 0, 0, 0x50, 0x3E, 0x4C, 0x75, 0xBA, 0x94, 0x31, 0x48, 0xF1, 0x8D, 0x94, 0x1E};
static const uint8_t uart_tx_uuid[]       = {0x71, 0x3D, 0, 3, 0x50, 0x3E, 0x4C, 0x75, 0xBA, 0x94, 0x31, 0x48, 0xF1, 0x8D, 0x94, 0x1E};
static const uint8_t uart_rx_uuid[]       = {0x71, 0x3D, 0, 2, 0x50, 0x3E, 0x4C, 0x75, 0xBA, 0x94, 0x31, 0x48, 0xF1, 0x8D, 0x94, 0x1E};
static const uint8_t uart_base_uuid_rev[] = {0x1E, 0x94, 0x8D, 0xF1, 0x48, 0x31, 0x94, 0xBA, 0x75, 0x4C, 0x3E, 0x50, 0, 0, 0x3D, 0x71};
uint8_t txPayload[TXRX_BUF_LEN] = {0,};
uint8_t rxPayload[TXRX_BUF_LEN] = {0,};

GattCharacteristic  txCharacteristic (uart_tx_uuid, txPayload, 1, TXRX_BUF_LEN,
                                      GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE_WITHOUT_RESPONSE);
                                      
GattCharacteristic  rxCharacteristic (uart_rx_uuid, rxPayload, 1, TXRX_BUF_LEN,
                                      GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY);
                                      
GattCharacteristic *uartChars[] = {&txCharacteristic, &rxCharacteristic};
GattService         uartService(uart_base_uuid, uartChars, sizeof(uartChars) / sizeof(GattCharacteristic *));

void disconnectionCallback(void)
{
    ble.startAdvertising();
}

void onDataWritten(uint16_t charHandle)
{	
    uint8_t buf[TXRX_BUF_LEN];
    uint16_t bytesRead;
	
    if (charHandle == txCharacteristic.getHandle()) 
    {
        ble.readCharacteristicValue(txCharacteristic.getHandle(), buf, &bytesRead);

        memset(txPayload, 0, TXRX_BUF_LEN);
        memcpy(txPayload, buf, TXRX_BUF_LEN);		

        if (buf[0] == 0x01)  // Command is to control digital out pin
        {
            if (buf[1] == 0x01)
                digitalWrite(DIGITAL_OUT_PIN, HIGH);
            else
                digitalWrite(DIGITAL_OUT_PIN, LOW);
        }
        else if (buf[0] == 0xA0) // Command is to enable analog in reading
        {
            if (buf[1] == 0x01)
              analog_enabled = true;
            else
              analog_enabled = false;
        }
        else if (buf[0] == 0x02) // Command is to control PWM pin
        {
            analogWrite(PWM_PIN, buf[1]);
        }
        else if (buf[0] == 0x03)  // Command is to control Servo pin
        {
            myservo.write(buf[1]);
        }
        else if (buf[0] == 0x04)
        {
            analog_enabled = false;
            myservo.write(0);
            analogWrite(PWM_PIN, 0);
            digitalWrite(DIGITAL_OUT_PIN, LOW);
            old_state = LOW;
        } 
    }
}
void m_status_check_handle(void * p_context)
{   
    uint8_t buf[3];
    if (analog_enabled)  // if analog reading enabled
    {
        // Read and send out
        uint16_t value = analogRead(ANALOG_IN_PIN); 
        buf[0] = (0x0B);
        buf[1] = (value >> 8);
        buf[2] = (value);
        ble.updateCharacteristicValue(rxCharacteristic.getHandle(), buf, 3);
    }
    // If digital in changes, report the state
    if (digitalRead(DIGITAL_IN_PIN) != old_state)
    {
        old_state = digitalRead(DIGITAL_IN_PIN);
        
        if (digitalRead(DIGITAL_IN_PIN) == HIGH)
        {
            buf[0] = (0x0A);
            buf[1] = (0x01);
            buf[2] = (0x00);    
            ble.updateCharacteristicValue(rxCharacteristic.getHandle(), buf, 3);
        }
        else
        {
            buf[0] = (0x0A);
            buf[1] = (0x00);
            buf[2] = (0x00);
            ble.updateCharacteristicValue(rxCharacteristic.getHandle(), buf, 3);
        }
    }
}

void setup(void)
{  
    uint32_t err_code = NRF_SUCCESS;
    
    delay(1000);
    //Serial.begin(9600);
    //Serial.println("Initialising the nRF51822\n\r");
    ble.init();
    ble.onDisconnection(disconnectionCallback);
    ble.onDataWritten(onDataWritten);

    // setup advertising 
    ble.accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED);
    ble.setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    ble.accumulateAdvertisingPayload(GapAdvertisingData::SHORTENED_LOCAL_NAME,
                                    (const uint8_t *)"Biscuit", sizeof("Biscuit") - 1);
    ble.accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LIST_128BIT_SERVICE_IDS,
                                    (const uint8_t *)uart_base_uuid_rev, sizeof(uart_base_uuid));
    // 100ms; in multiples of 0.625ms. 
    ble.setAdvertisingInterval(160);

    ble.addService(uartService);
    
    ble.startAdvertising();
    
    pinMode(DIGITAL_OUT_PIN, OUTPUT);
    pinMode(DIGITAL_IN_PIN, INPUT_PULLUP);
    pinMode(PWM_PIN, OUTPUT);
    //pinMode(13, OUTPUT);
    // Default to internally pull high, change it if you need
    digitalWrite(DIGITAL_IN_PIN, HIGH);
    
    myservo.attach(SERVO_PIN);
    myservo.write(0);

    err_code = app_timer_create(&m_status_check_id,APP_TIMER_MODE_REPEATED, m_status_check_handle);
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_start(m_status_check_id, STATUS_CHECK_TIME, NULL);
    APP_ERROR_CHECK(err_code);    
    
    //Serial.println("Advertising Start!");
}

void loop(void)
{
    ble.waitForEvent();
}
