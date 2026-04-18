- Added persistent mobile push notification that has sounds

Big system architecture shift:
- Planning to use water pump instead of solenoid valve. 
- Instead of 2 water pump (previously solenoid lamp) we now need four.
- Since I added two more water pump, I will also be needing two more relay module.
- Since we ran out of pins on our esp32-c3 supermini, I am planning to use Arduino Uno and make it communicate to esp32-c3 super mini via serial communication.
- Moreover, I am planning to replace the DHT11 sensor with DS18B20 temperature sensor for more accurate temperature readings.

Here are the components that I want to be connected on
ESP32-C3 Super Mini:
- Same as before only transferring some components to the arduino uno. So removed components from esp32-c3 and transferred to uno are  capacitive soil moisture sensor for zone 1 and 2, ultrasonic sensor for water level sensing,  and rs485 to ttl converter.

- What stays in esp32-c3 super mini are the LCD display and  4 relay for the water pump and all the software features including the dashboard and wifi soft ap and etc.


For Arduino Uno, this will be the components  capacitive soil moisture sensor for zone 1 and 2, ultrasonic sensor for water level sensing,  and rs485 to ttl converter.


For Dashboard adjustments:

- Add a feature/button to manually control the water pump or manually water the zone. The layout is since I have two zones, there will be two water pump with hose connected to each zone so each zone has two hose. So, I have 4 buttons, each button can manually control the app like when I push a button it will pump the water for several seconds or optimal time for watering and after several optimal seconds of watering, the relay will close. So it is combination of automation and manual intervention.:
